#include "Sim/Upgrades.h"

#include "Banner/BannerController.h"
#include "Core/Factions.h"
#include "Core/I18n.h"
#include "Core/State.h"
#include "Core/Timer.h"
#include "Sim/Economy.h"
#include "Stock/Stock.h"
#include "World/Conquest.h"
#include "World/Placement.h"
#include "World/Seat.h"

namespace AoS
{
    namespace
    {
        const fs::path  kRoot     = "Data/SKSE/Plugins/AgeOfSkyrim";
        constexpr float kDegToRad = 3.14159265f / 180.0f;
        constexpr std::size_t kMaxLevels = 10;

        constexpr std::array<std::string_view, kStatCount> kStatNames{ "defense", "capacity", "income", "vision",
                                                                       "garrisonMax" };

        std::optional<nlohmann::json> ReadJson(const fs::path& a_file)
        {
            std::ifstream in(a_file);
            if (!in) {
                return std::nullopt;
            }
            try {
                return nlohmann::json::parse(in, nullptr, true, /*ignore_comments*/ true);
            } catch (const std::exception& e) {
                logger::error("JSON inválido em {}: {}", a_file.string(), e.what());
                return std::nullopt;
            }
        }

        // "Plugin.esp|0xID" no formato de Discovery::KeyOf (territórios descobertos); senão o id do JSON.
        Id TerritoryKey(const std::string& a_text)
        {
            const auto ref = FormRef::Parse(a_text);
            return ref ? MakeId(std::format("{}|0x{:06X}", ref->plugin, ref->localId)) : MakeId(a_text);
        }

        std::string Roman(std::uint32_t a_level)
        {
            static constexpr std::array<std::string_view, 11> kRoman{ "0", "I",   "II",   "III", "IV", "V",
                                                                      "VI", "VII", "VIII", "IX",  "X" };
            return a_level < kRoman.size() ? std::string(kRoman[a_level]) : std::to_string(a_level);
        }

        bool IsPlayerFaction(Id a_faction)
        {
            const auto f = Factions::Get().Find(a_faction);
            return f && f->player;
        }

        bool CellAttached(const RE::TESObjectREFR* a_ref)
        {
            const auto cell = a_ref ? a_ref->GetParentCell() : nullptr;
            return cell && cell->IsAttached() && !a_ref->IsDeleted();
        }

        // Só agenda trabalho se há objetos visuais esperando a âncora carregar (custo zero no caso comum).
        class CellSink final : public RE::BSTEventSink<RE::TESCellFullyLoadedEvent>
        {
        public:
            static CellSink* Get()
            {
                static CellSink instance;
                return std::addressof(instance);
            }

            RE::BSEventNotifyControl ProcessEvent(const RE::TESCellFullyLoadedEvent*,
                                                  RE::BSTEventSource<RE::TESCellFullyLoadedEvent>*) override
            {
                Upgrades::Get().RequestVisuals();
                return RE::BSEventNotifyControl::kContinue;
            }
        };
    }

    std::string_view ToString(Stat a_stat) noexcept
    {
        const auto i = static_cast<std::size_t>(a_stat);
        return i < kStatNames.size() ? kStatNames[i] : "?";
    }

    std::optional<Stat> ParseStat(std::string_view a_text) noexcept
    {
        for (std::size_t i = 0; i < kStatNames.size(); ++i) {
            if (a_text.size() == kStatNames[i].size() &&
                _strnicmp(a_text.data(), kStatNames[i].data(), a_text.size()) == 0) {
                return static_cast<Stat>(i);
            }
        }
        return std::nullopt;
    }

    Upgrades& Upgrades::Get() noexcept
    {
        static Upgrades instance;
        return instance;
    }

    // ---- Dados ------------------------------------------------------------------------------------

    void Upgrades::Load()
    {
        ScopedTimer timer{ "Upgrades::Load" };
        {
            std::scoped_lock l{ lock_ };
            upgrades_.clear();
            index_.clear();
            visuals_.clear();
            visualsByTerritory_.clear();

            // Upgrades.json primeiro, depois Upgrades/*.json em ordem alfabética (mesmo "id" substitui).
            std::vector<fs::path> files{ kRoot / "Upgrades.json" };
            std::vector<fs::path> extra;
            std::error_code       ec;
            for (const auto& entry : fs::directory_iterator(kRoot / "Upgrades", ec)) {
                if (entry.is_regular_file() && entry.path().extension() == ".json") {
                    extra.push_back(entry.path());
                }
            }
            std::sort(extra.begin(), extra.end());
            files.insert(files.end(), extra.begin(), extra.end());
            for (const auto& f : files) {
                try {
                    LoadFile(f);
                } catch (const std::exception& e) {
                    logger::error("Melhorias: erro em {}: {}", f.filename().string(), e.what());
                }
            }
            for (std::size_t i = 0; i < visuals_.size(); ++i) {
                visualsByTerritory_.emplace(visuals_[i].territory, i);
            }
            logger::info("Melhorias: {} melhoria(s), {} conjunto(s) visual(is), defesa em obra x{:.2f}.",
                         upgrades_.size(), visuals_.size(), constructionDefense_);
        }

        auto& eco = Economy::Get();
        eco.AddTickListener("upgrades", [](std::uint32_t a_hours) { Upgrades::Get().OnHours(a_hours); });
        eco.SetTerritoryMultiplier(
            [](Id a_territory) { return 1.0 + Upgrades::Get().Stat(a_territory, AoS::Stat::kIncome) / 100.0; });

        static bool registered = false;
        if (!registered) {
            if (const auto holder = RE::ScriptEventSourceHolder::GetSingleton()) {
                holder->AddEventSink<RE::TESCellFullyLoadedEvent>(CellSink::Get());
            }
            registered = true;
        }
    }

    void Upgrades::LoadFile(const fs::path& a_file)
    {
        const auto json = ReadJson(a_file);
        if (!json || !json->is_object()) {
            if (a_file.filename() == "Upgrades.json") {
                logger::warn("Melhorias: {} ausente ou inválido; nenhuma melhoria disponível.", a_file.string());
            }
            return;
        }
        const auto name = a_file.filename().string();
        constructionDefense_ = json->value("constructionDefenseMultiplier", constructionDefense_);

        const auto& stock = Stock::Get();
        for (const auto& ju : json->value("upgrades", nlohmann::json::array())) {
            if (!ju.is_object()) {
                continue;
            }
            Upgrade u;
            u.key = ju.value("id", std::string{});
            if (u.key.empty()) {
                logger::warn("{}: melhoria sem 'id'; ignorada.", name);
                continue;
            }
            u.id   = MakeId(u.key);
            u.name = ju.contains("name") ? I18n::Pick(ju["name"], u.key) : u.key;
            for (const auto& t : ju.value("types", std::vector<std::string>{})) {
                const auto type = t == "unknown" ? std::optional{ TerritoryType::kUnknown } : ParseTerritoryType(t);
                if (type) {
                    u.types.push_back(*type);
                } else {
                    logger::warn("{}: melhoria '{}': tipo '{}' inválido; ignorado.", name, u.key, t);
                }
            }
            for (const auto& jl : ju.value("levels", nlohmann::json::array())) {
                if (!jl.is_object() || u.levels.size() >= kMaxLevels) {
                    continue;
                }
                LevelSpec level;
                level.hours = std::max(0.0, jl.value("hours", 0.0));
                if (const auto c = jl.find("cost"); c != jl.end() && c->is_object()) {
                    for (const auto& [cat, amount] : c->items()) {
                        const auto idx = stock.CategoryIndex(cat);
                        if (!idx || !amount.is_number_integer()) {
                            logger::warn("{}: '{}' nível {}: categoria '{}' desconhecida em Resources.json (ou "
                                         "quantidade não inteira); custo ignorado.",
                                         name, u.key, u.levels.size() + 1, cat);
                            continue;
                        }
                        if (const auto n = amount.get<std::int64_t>(); n > 0) {
                            level.cost.emplace_back(*idx, n);
                        }
                    }
                }
                if (const auto s = jl.find("stats"); s != jl.end() && s->is_object()) {
                    for (const auto& [statKey, value] : s->items()) {
                        const auto stat = ParseStat(statKey);
                        if (!stat || !value.is_number()) {
                            logger::warn("{}: '{}': status '{}' desconhecido (use defense, capacity, income, vision, "
                                         "garrisonMax).",
                                         name, u.key, statKey);
                            continue;
                        }
                        level.stats[static_cast<std::size_t>(*stat)] = value.get<double>();
                    }
                }
                u.levels.push_back(std::move(level));
            }
            if (u.levels.empty()) {
                logger::warn("{}: melhoria '{}' sem níveis; ignorada.", name, u.key);
                continue;
            }
            logger::info("Melhorias: '{}' ({}) com {} nível(is), {} tipo(s) de território.", u.key, u.name,
                         u.levels.size(), u.types.empty() ? "todos"s : std::to_string(u.types.size()));
            if (const auto it = index_.find(u.id); it != index_.end()) {
                upgrades_[it->second] = std::move(u);
            } else {
                index_.emplace(u.id, upgrades_.size());
                upgrades_.push_back(std::move(u));
            }
        }

        // "visuals": objetos extras por território/melhoria/nível, com offsets relativos à âncora.
        for (const auto& jv : json->value("visuals", nlohmann::json::array())) {
            if (!jv.is_object()) {
                continue;
            }
            Visual v;
            const auto terrKey = jv.value("territory", std::string{});
            const auto upgKey  = jv.value("upgrade", std::string{});
            if (terrKey.empty() || upgKey.empty()) {
                logger::warn("{}: visual sem 'territory' ou 'upgrade'; ignorado.", name);
                continue;
            }
            v.territory = TerritoryKey(terrKey);
            v.upgrade   = MakeId(upgKey);
            v.level     = static_cast<std::uint8_t>(std::clamp(jv.value("level", 1), 1, static_cast<int>(kMaxLevels)));
            const auto anchor = jv.value("anchor", "auto"s);
            v.anchor = anchor == "marker" ? Anchor::kMarker : anchor == "banner" ? Anchor::kBanner : Anchor::kAuto;
            for (const auto& jo : jv.value("objects", nlohmann::json::array())) {
                const auto base = jo.is_object() ? FormRef::Parse(jo.value("base", std::string{})) : std::nullopt;
                const auto off  = jo.is_object() ? jo.value("offset", std::vector<float>{}) : std::vector<float>{};
                const auto ang  = jo.is_object() ? jo.value("angle", std::vector<float>{}) : std::vector<float>{};
                if (!base || off.size() != 3 || (!ang.empty() && ang.size() != 3)) {
                    logger::warn("{}: visual '{}/{}': objeto inválido (use {{\"base\": \"Plugin.esp|0xID\", \"offset\": "
                                 "[x,y,z], \"angle\": [x,y,z]}}).",
                                 name, terrKey, upgKey);
                    continue;
                }
                VisualObject o;
                o.key    = MakeId(std::format("upgr/{}/{}/{}/{}", terrKey, upgKey, v.level, v.objects.size()));
                o.base   = *base;
                o.offset = { off[0], off[1], off[2] };
                if (ang.size() == 3) {
                    o.angle = { ang[0] * kDegToRad, ang[1] * kDegToRad, ang[2] * kDegToRad };
                }
                o.object = base->Resolve<RE::TESBoundObject>();
                if (!o.object) {
                    logger::warn("{}: visual '{}/{}': objeto {}|0x{:X} não encontrado (plugin ausente?).", name,
                                 terrKey, upgKey, base->plugin, base->localId);
                    continue;
                }
                v.objects.push_back(std::move(o));
            }
            if (!v.objects.empty()) {
                visuals_.push_back(std::move(v));
            }
        }
    }

    // ---- Consulta ----------------------------------------------------------------------------------

    const Upgrades::Upgrade* Upgrades::Find(Id a_upgrade) const
    {
        const auto it = index_.find(a_upgrade);
        return it == index_.end() ? nullptr : &upgrades_[it->second];
    }

    bool Upgrades::Allows(const Upgrade& a_upgrade, TerritoryType a_type) const
    {
        return a_upgrade.types.empty() ||
               std::find(a_upgrade.types.begin(), a_upgrade.types.end(), a_type) != a_upgrade.types.end();
    }

    double Upgrades::Stat(Id a_territory, AoS::Stat a_stat) const
    {
        const auto i = static_cast<std::size_t>(a_stat);
        if (i >= kStatCount) {
            return 0.0;
        }
        std::scoped_lock l{ lock_ };
        const auto       it = states_.find(a_territory);
        return it == states_.end() ? 0.0 : it->second.stats[i];
    }

    std::uint8_t Upgrades::Level(Id a_territory, Id a_upgrade) const
    {
        std::scoped_lock l{ lock_ };
        const auto       it = states_.find(a_territory);
        if (it == states_.end()) {
            return 0;
        }
        const auto lv = it->second.levels.find(a_upgrade);
        return lv == it->second.levels.end() ? 0 : lv->second;
    }

    bool Upgrades::InConstruction(Id a_territory) const
    {
        std::scoped_lock l{ lock_ };
        const auto       it = states_.find(a_territory);
        return it != states_.end() && it->second.construction.has_value();
    }

    std::optional<Upgrades::Construction> Upgrades::CurrentConstruction(Id a_territory) const
    {
        std::scoped_lock l{ lock_ };
        const auto       it = states_.find(a_territory);
        return it == states_.end() ? std::nullopt : it->second.construction;
    }

    Id Upgrades::NextAvailable(Id a_territory) const
    {
        const auto t = Config::Get().Find(a_territory);
        if (!t) {
            return kNeutral;
        }
        for (const auto& u : upgrades_) {
            if (Allows(u, t->type) && Level(a_territory, u.id) < u.levels.size()) {
                return u.id;
            }
        }
        return kNeutral;
    }

    std::string Upgrades::CostText(const LevelSpec& a_level) const
    {
        const auto& cats = Stock::Get().Categories();
        std::string out;
        for (const auto& [idx, amount] : a_level.cost) {
            out += std::format("{}{} {}", out.empty() ? "" : ", ", idx < cats.size() ? cats[idx].name : "?", amount);
        }
        return out.empty() ? "-"s : out;
    }

    void Upgrades::Recompute(Id, TerritoryState& a_state) const
    {
        a_state.stats.fill(0.0);
        for (const auto& [upgrade, level] : a_state.levels) {
            const auto u = Find(upgrade);
            if (!u || level == 0) {
                continue;
            }
            const auto& spec = u->levels[std::min<std::size_t>(level, u->levels.size()) - 1];
            for (std::size_t i = 0; i < kStatCount; ++i) {
                a_state.stats[i] += spec.stats[i];
            }
        }
    }

    // ---- Obras ---------------------------------------------------------------------------------------

    Upgrades::Result Upgrades::Start(Id a_territory, Id a_upgrade, bool a_payCost)
    {
        ScopedTimer timer{ "Upgrades::Start" };
        const auto  t = Config::Get().Find(a_territory);
        if (!t) {
            return Result::kUnknownTerritory;
        }
        const auto u = Find(a_upgrade);
        if (!u) {
            return Result::kUnknownUpgrade;
        }
        if (!Allows(*u, t->type)) {
            return Result::kWrongType;
        }
        const auto owner = State::Get().Owner(a_territory);
        if (owner == kNeutral) {
            return Result::kNoOwner;
        }
        if (InConstruction(a_territory)) {
            return Result::kBusy;
        }
        const auto current = Level(a_territory, a_upgrade);
        if (current >= u->levels.size()) {
            return Result::kMaxLevel;
        }
        const auto& spec = u->levels[current];

        // Facção do jogador paga com itens reais do baú da sede (thread principal: lê/altera o inventário).
        if (a_payCost && IsPlayerFaction(owner) && !spec.cost.empty()) {
            const auto chest = Seat::Get().Chest();
            if (!chest) {
                return Result::kNoSeat;
            }
            auto&      stock  = Stock::Get();
            const auto totals = stock.CountByCategory(chest);
            for (const auto& [idx, amount] : spec.cost) {
                if (idx >= totals.perCategory.size() || totals.perCategory[idx] < amount) {
                    return Result::kInsufficient;
                }
            }
            for (const auto& [idx, amount] : spec.cost) {
                stock.Take(chest, idx, amount);
            }
            if (stock.IsObserved(chest->GetFormID())) {
                stock.Recount(chest->GetFormID());
            }
        }

        const auto level = static_cast<std::uint8_t>(current + 1);
        {
            std::scoped_lock l{ lock_ };
            auto&            state = states_[a_territory];
            if (spec.hours <= 0.0) {
                state.levels[a_upgrade] = level;
                Recompute(a_territory, state);
                MarkVisualsPending(a_territory);
            } else {
                state.construction = Construction{ a_upgrade, level, spec.hours, spec.hours, owner };
            }
        }
        logger::info("Melhorias: obra '{}' nível {} iniciada em '{}' por '{}' ({} h, custo: {}{}).", u->key, level,
                     t->key, Factions::Get().NameOf(owner), spec.hours, CostText(spec), a_payCost ? "" : ", grátis");
        if (spec.hours <= 0.0) {
            RequestVisuals();
        }
        return Result::kOk;
    }

    void Upgrades::OnHours(std::uint32_t a_hours)
    {
        ScopedTimer timer{ "Upgrades::OnHours" };
        struct Done
        {
            Id           territory;
            Id           upgrade;
            std::uint8_t level;
            Id           owner;
            bool         cancelled;
        };
        std::vector<Done> done;
        {
            std::scoped_lock l{ lock_ };
            const auto&      state = State::Get();
            for (auto& [territory, ts] : states_) {
                if (!ts.construction) {
                    continue;
                }
                auto& c = *ts.construction;
                if (state.Owner(territory) != c.owner) {
                    // Território mudou de dono no meio da obra: a obra se perde (sem reembolso).
                    done.push_back({ territory, c.upgrade, c.level, c.owner, true });
                    ts.construction.reset();
                    continue;
                }
                c.remaining -= a_hours;
                if (c.remaining > 1e-9) {
                    continue;
                }
                ts.levels[c.upgrade] = c.level;
                done.push_back({ territory, c.upgrade, c.level, c.owner, false });
                ts.construction.reset();
                Recompute(territory, ts);
                MarkVisualsPending(territory);
            }
        }
        for (const auto& d : done) {
            const auto t = Config::Get().Find(d.territory);
            const auto u = Find(d.upgrade);
            if (!t || !u) {
                continue;
            }
            logger::info("Melhorias: obra '{}' nível {} em '{}' {}.", u->key, d.level, t->key,
                         d.cancelled ? "cancelada (troca de dono)" : "concluída");
            if (IsPlayerFaction(d.owner)) {
                I18n::NotifyTr(d.cancelled ? "upgrade.cancelled" : "upgrade.done", u->name, Roman(d.level), t->name);
            }
        }
        if (!done.empty()) {
            RequestVisuals();
        }
    }

    // ---- Visual por nível -------------------------------------------------------------------------------

    void Upgrades::MarkVisualsPending(Id a_territory)
    {
        if (visualsByTerritory_.contains(a_territory)) {
            pendingVisuals_.insert(a_territory);
            anyPending_ = true;
        }
    }

    RE::TESObjectREFR* Upgrades::FindAnchor(const Territory& a_territory, Anchor a_anchor)
    {
        if (a_anchor != Anchor::kBanner && a_territory.location) {
            if (const auto marker = a_territory.location->worldLocMarker.get(); marker && CellAttached(marker.get())) {
                return marker.get();
            }
        }
        if (a_anchor != Anchor::kMarker) {
            RE::TESObjectREFR* found = nullptr;
            for (const auto& set : a_territory.banners) {
                ForEachBanner(set, [&](RE::TESObjectREFR* a_ref) {
                    if (!found && CellAttached(a_ref)) {
                        found = a_ref;
                    }
                });
                if (found) {
                    return found;
                }
            }
        }
        return nullptr;
    }

    void Upgrades::RequestVisuals()
    {
        if (!anyPending_.load() || queued_.exchange(true)) {
            return;
        }
        SKSE::GetTaskInterface()->AddTask([] {
            Upgrades::Get().queued_ = false;
            Upgrades::Get().ApplyVisuals();
        });
    }

    void Upgrades::ApplyVisuals()
    {
        if (!anyPending_.load()) {
            return;
        }
        ScopedTimer timer{ "Upgrades::ApplyVisuals" };
        std::scoped_lock l{ lock_ };
        std::size_t      created = 0;
        for (auto it = pendingVisuals_.begin(); it != pendingVisuals_.end();) {
            const auto territory = *it;
            const auto t         = Config::Get().Find(territory);
            const auto st        = states_.find(territory);
            if (!t || st == states_.end()) {
                it = pendingVisuals_.erase(it);
                continue;
            }
            bool complete = true;
            const auto [first, last] = visualsByTerritory_.equal_range(territory);
            for (auto v = first; v != last; ++v) {
                const auto& visual = visuals_[v->second];
                const auto  lv     = st->second.levels.find(visual.upgrade);
                if (lv == st->second.levels.end() || lv->second < visual.level) {
                    continue;
                }
                RE::TESObjectREFR* anchor = nullptr;
                for (const auto& o : visual.objects) {
                    if (const auto r = visualRefs_.find(o.key); r != visualRefs_.end()) {
                        const auto ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(r->second);
                        if (ref && !ref->IsDeleted()) {
                            continue;
                        }
                    }
                    if (!anchor) {
                        anchor = FindAnchor(*t, visual.anchor);
                        if (!anchor) {
                            complete = false;  // âncora não carregada: tenta na próxima célula carregada
                            break;
                        }
                    }
                    // Offset no referencial da âncora: x = direita, y = frente (heading = rotação em Z).
                    const float  h   = anchor->GetAngleZ();
                    RE::NiPoint3 pos = anchor->GetPosition();
                    pos.x += o.offset.x * std::cos(h) + o.offset.y * std::sin(h);
                    pos.y += -o.offset.x * std::sin(h) + o.offset.y * std::cos(h);
                    pos.z += o.offset.z;
                    const RE::NiPoint3 angle{ o.angle.x, o.angle.y, o.angle.z + h };
                    if (const auto ref = Placement::PlaceAt(o.object, pos, angle, /*keyframed*/ true, anchor)) {
                        visualRefs_[o.key] = ref->GetFormID();
                        ++created;
                    } else {
                        complete = false;
                    }
                }
            }
            it = complete ? pendingVisuals_.erase(it) : std::next(it);
        }
        anyPending_ = !pendingVisuals_.empty();
        if (created > 0) {
            logger::info("Melhorias: {} objeto(s) visual(is) criado(s); {} território(s) ainda esperando a âncora.",
                         created, pendingVisuals_.size());
        }
    }

    // ---- Debug --------------------------------------------------------------------------------------------

    void Upgrades::DebugHere()
    {
        auto t = Conquest::Get().TerritoryAtPlayer(/*onlyCleared*/ false);
        if (!t || !IsPlayerFaction(State::Get().Owner(t->id))) {
            if (const auto seat = Seat::Get().SeatTerritory(); seat != kNeutral) {
                t = Config::Get().Find(seat);
            } else if (!t) {
                t = Config::Get().Find(Config::Get().Debug().territory);
            }
        }
        if (!t) {
            I18n::NotifyTr("upgrade.noTerritory");
            return;
        }
        if (!IsPlayerFaction(State::Get().Owner(t->id))) {
            I18n::NotifyTr("upgrade.notYours", t->name);
            return;
        }
        if (const auto c = CurrentConstruction(t->id)) {
            const auto u = Find(c->upgrade);
            I18n::NotifyTr("upgrade.inProgress", u ? u->name : "?"s, Roman(c->level), t->name,
                           std::format("{:.0f}", std::max(0.0, c->remaining)));
        } else if (const auto next = NextAvailable(t->id); next == kNeutral) {
            I18n::NotifyTr("upgrade.allDone", t->name);
        } else {
            const auto  u     = Find(next);
            const auto  level = Level(t->id, next);
            const auto& spec  = u->levels[level];
            switch (Start(t->id, next, !Config::Get().Debug().upgradeIgnoreCost)) {
            case Result::kOk:
                I18n::NotifyTr("upgrade.started", u->name, Roman(level + 1u), t->name,
                               std::format("{:.0f}", spec.hours));
                break;
            case Result::kNoSeat:
                I18n::NotifyTr("upgrade.noSeat");
                break;
            case Result::kInsufficient:
                I18n::NotifyTr("upgrade.insufficient", u->name, Roman(level + 1u), CostText(spec));
                break;
            default:
                I18n::NotifyTr("upgrade.failed", u->name);
                break;
            }
        }
        I18n::NotifyTr("upgrade.stats", std::format("{:.0f}", Stat(t->id, AoS::Stat::kDefense)),
                       std::format("{:.0f}", Stat(t->id, AoS::Stat::kCapacity)),
                       std::format("{:.0f}", Stat(t->id, AoS::Stat::kIncome)),
                       std::format("{:.0f}", Stat(t->id, AoS::Stat::kVision)),
                       std::format("{:.0f}", Stat(t->id, AoS::Stat::kGarrisonMax)));
    }

    // ---- Cosave --------------------------------------------------------------------------------------------

    void Upgrades::OnPostLoad()
    {
        {
            std::scoped_lock l{ lock_ };
            for (const auto& [territory, ts] : states_) {
                if (!ts.levels.empty()) {
                    MarkVisualsPending(territory);  // confere se os objetos ainda existem quando a âncora carregar
                }
            }
        }
        RequestVisuals();
    }

    void Upgrades::Clear()
    {
        std::scoped_lock l{ lock_ };
        states_.clear();
        visualRefs_.clear();
        pendingVisuals_.clear();
        anyPending_ = false;
    }

    // Formato 'UPGR' v1: u32 territórios × { Id território, u32 n × { Id melhoria, u8 nível }, u8 temObra,
    // [Id melhoria, u8 nível, f64 restante, f64 total, Id dono] }, depois u32 refs × { Id chave, FormID }.
    void Upgrades::Save(SKSE::SerializationInterface* a_intfc) const
    {
        std::scoped_lock l{ lock_ };
        if (states_.empty() && visualRefs_.empty()) {
            return;
        }
        if (!a_intfc->OpenRecord(kRecord, kRecordVersion)) {
            logger::error("Cosave: falha ao abrir o registro UPGR.");
            return;
        }
        a_intfc->WriteRecordData(static_cast<std::uint32_t>(states_.size()));
        for (const auto& [territory, ts] : states_) {
            a_intfc->WriteRecordData(territory);
            a_intfc->WriteRecordData(static_cast<std::uint32_t>(ts.levels.size()));
            for (const auto& [upgrade, level] : ts.levels) {
                a_intfc->WriteRecordData(upgrade);
                a_intfc->WriteRecordData(level);
            }
            const std::uint8_t has = ts.construction ? 1 : 0;
            a_intfc->WriteRecordData(has);
            if (ts.construction) {
                a_intfc->WriteRecordData(ts.construction->upgrade);
                a_intfc->WriteRecordData(ts.construction->level);
                a_intfc->WriteRecordData(ts.construction->remaining);
                a_intfc->WriteRecordData(ts.construction->total);
                a_intfc->WriteRecordData(ts.construction->owner);
            }
        }
        a_intfc->WriteRecordData(static_cast<std::uint32_t>(visualRefs_.size()));
        for (const auto& [key, formId] : visualRefs_) {
            a_intfc->WriteRecordData(key);
            a_intfc->WriteRecordData(formId);
        }
    }

    void Upgrades::LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version)
    {
        if (a_version != kRecordVersion) {
            logger::warn("Cosave: versão {} do registro UPGR não suportada; melhorias recomeçam.", a_version);
            return;
        }
        std::scoped_lock l{ lock_ };
        std::uint32_t    count{};
        std::size_t      dropped = 0;
        a_intfc->ReadRecordData(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            Id            territory{};
            std::uint32_t levels{};
            a_intfc->ReadRecordData(territory);
            a_intfc->ReadRecordData(levels);
            TerritoryState ts;
            for (std::uint32_t j = 0; j < levels; ++j) {
                Id           upgrade{};
                std::uint8_t level{};
                a_intfc->ReadRecordData(upgrade);
                a_intfc->ReadRecordData(level);
                if (Find(upgrade)) {
                    ts.levels[upgrade] = level;
                } else {
                    ++dropped;  // melhoria removida do JSON
                }
            }
            std::uint8_t has{};
            a_intfc->ReadRecordData(has);
            if (has) {
                Construction c;
                a_intfc->ReadRecordData(c.upgrade);
                a_intfc->ReadRecordData(c.level);
                a_intfc->ReadRecordData(c.remaining);
                a_intfc->ReadRecordData(c.total);
                a_intfc->ReadRecordData(c.owner);
                if (Find(c.upgrade)) {
                    ts.construction = c;
                } else {
                    ++dropped;
                }
            }
            Recompute(territory, ts);
            states_[territory] = std::move(ts);
        }
        std::uint32_t refs{};
        a_intfc->ReadRecordData(refs);
        for (std::uint32_t i = 0; i < refs; ++i) {
            Id         key{};
            RE::FormID oldId{}, newId{};
            a_intfc->ReadRecordData(key);
            a_intfc->ReadRecordData(oldId);
            if (a_intfc->ResolveFormID(oldId, newId)) {
                visualRefs_[key] = newId;
            }
        }
        logger::info("Cosave: melhorias de {} território(s), {} objeto(s) visual(is) ({} item(ns) descartado(s)).",
                     states_.size(), visualRefs_.size(), dropped);
    }
}
