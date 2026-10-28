#include "Sim/Garrison.h"

#include "Banner/BannerController.h"
#include "Core/Factions.h"
#include "Core/I18n.h"
#include "Core/State.h"
#include "Core/Timer.h"
#include "Sim/Economy.h"
#include "Sim/Upgrades.h"
#include "World/Conquest.h"
#include "World/Seat.h"

namespace AoS
{
    namespace
    {
        const fs::path        kRoot         = "Data/SKSE/Plugins/AgeOfSkyrim";
        constexpr RE::FormID  kPlayerFaction = 0x00000DB1;  // PlayerFaction (Skyrim.esm)
        constexpr float       kGoldenAngle  = 2.39996323f;  // espalha os soldados em volta do marcador

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

        std::string Lower(std::string_view a_text)
        {
            std::string out{ a_text };
            std::ranges::transform(out, out.begin(), [](char c) { return static_cast<char>(std::tolower(c)); });
            return out;
        }

        // "Plugin.esp|0xID" (FormID local) ou EditorID. EditorID de NPC/LVLN/OTFT/FACT só fica na memória com
        // po3 Tweaks (ou similar); sem ele a busca falha e o próximo candidato é tentado (tudo logado).
        RE::TESForm* ResolveForm(const std::string& a_text)
        {
            if (a_text.find('|') != std::string::npos) {
                const auto ref = FormRef::Parse(a_text);
                return ref ? ref->Resolve<RE::TESForm>() : nullptr;
            }
            return a_text.empty() ? nullptr : RE::TESForm::LookupByEditorID(a_text);
        }

        std::vector<std::string> Candidates(const nlohmann::json& a_obj, const char* a_field)
        {
            const auto it = a_obj.find(a_field);
            if (it == a_obj.end()) {
                return {};
            }
            if (it->is_string()) {
                return { it->get<std::string>() };
            }
            return it->is_array() ? it->get<std::vector<std::string>>() : std::vector<std::string>{};
        }

        template <class T>
        T* ResolveFirst(const std::vector<std::string>& a_candidates, RE::FormType a_type, std::string_view a_what,
                        std::string_view a_troop)
        {
            for (const auto& c : a_candidates) {
                const auto form = ResolveForm(c);
                if (form && form->Is(a_type)) {
                    logger::info("Guarnição: tropa '{}': {} '{}' -> 0x{:08X} ('{}').", a_troop, a_what, c,
                                 form->GetFormID(), form->GetName());
                    return static_cast<T*>(form);
                }
                logger::info("Guarnição: tropa '{}': {} '{}' não encontrado (plugin ausente / EditorID sem po3 "
                             "Tweaks / tipo errado).",
                             a_troop, a_what, c);
            }
            return nullptr;
        }

        bool ArmorMatches(RE::TESForm* a_item, const std::string& a_hint, int a_depth)
        {
            if (!a_item || a_depth > 2) {
                return false;
            }
            if (const auto armo = a_item->As<RE::TESObjectARMO>()) {
                const auto model = armo->worldModels[0].GetModel();
                return model && Lower(model).find(a_hint) != std::string::npos;
            }
            if (const auto lev = a_item->As<RE::TESLevItem>()) {
                for (const auto f : lev->GetContainedForms()) {
                    if (ArmorMatches(f, a_hint, a_depth + 1)) {
                        return true;
                    }
                }
            }
            return false;
        }

        // Escolha automática (sem FormIDs de memória): NPC genérico (não único) do Skyrim.esm, de raça jogável,
        // com nome, cujo outfit padrão tem armadura com a dica no caminho do modelo ("imperial", "stormcloak").
        // Ordem do arquivo = determinística. NPC do Skyrim.esm = FaceGen pronto no BSA vanilla.
        RE::TESNPC* AutoSoldier(const std::string& a_hint)
        {
            const auto dh = RE::TESDataHandler::GetSingleton();
            if (!dh || a_hint.empty()) {
                return nullptr;
            }
            for (const auto npc : dh->GetFormArray<RE::TESNPC>()) {
                const auto file = npc ? npc->GetFile(0) : nullptr;
                if (!file || _stricmp(file->fileName, "Skyrim.esm") != 0 || npc->IsUnique() || !npc->defaultOutfit) {
                    continue;
                }
                const auto name = npc->GetName();
                if (!name || !*name || !npc->race || !npc->race->GetPlayable()) {
                    continue;
                }
                for (const auto item : npc->defaultOutfit->outfitItems) {
                    if (ArmorMatches(item, a_hint, 0)) {
                        return npc;
                    }
                }
            }
            return nullptr;
        }

        bool CellAttached(const RE::TESObjectREFR* a_ref)
        {
            const auto cell = a_ref ? a_ref->GetParentCell() : nullptr;
            return cell && cell->IsAttached() && !a_ref->IsDeleted();
        }

        bool IsPlayerFaction(Id a_faction)
        {
            const auto f = Factions::Get().Find(a_faction);
            return f && f->player;
        }

        float Distance2D(const RE::NiPoint3& a_a, const RE::NiPoint3& a_b)
        {
            const auto dx = a_a.x - a_b.x;
            const auto dy = a_a.y - a_b.y;
            return std::sqrt(dx * dx + dy * dy);
        }

        // Eventos que já acontecem: célula carregada / jogador mudou de local -> refresh coalescido;
        // morte e descarga de célula só fazem trabalho se o ator é nosso (refs criadas em runtime são 0xFF......).
        class WorldSink final :
            public RE::BSTEventSink<RE::TESCellFullyLoadedEvent>,
            public RE::BSTEventSink<RE::TESActorLocationChangeEvent>,
            public RE::BSTEventSink<RE::TESDeathEvent>,
            public RE::BSTEventSink<RE::TESCellAttachDetachEvent>
        {
        public:
            static WorldSink* Get()
            {
                static WorldSink instance;
                return std::addressof(instance);
            }

            RE::BSEventNotifyControl ProcessEvent(const RE::TESCellFullyLoadedEvent*,
                                                  RE::BSTEventSource<RE::TESCellFullyLoadedEvent>*) override
            {
                Garrison::Get().RequestRefresh();
                return RE::BSEventNotifyControl::kContinue;
            }

            RE::BSEventNotifyControl ProcessEvent(const RE::TESActorLocationChangeEvent* a_event,
                                                  RE::BSTEventSource<RE::TESActorLocationChangeEvent>*) override
            {
                if (a_event && a_event->actor && a_event->actor->IsPlayerRef()) {
                    Garrison::Get().RequestRefresh();
                }
                return RE::BSEventNotifyControl::kContinue;
            }

            RE::BSEventNotifyControl ProcessEvent(const RE::TESDeathEvent* a_event,
                                                  RE::BSTEventSource<RE::TESDeathEvent>*) override
            {
                if (a_event && a_event->dead && a_event->actorDying && a_event->actorDying->IsDynamicForm()) {
                    Garrison::Get().OnActorDeath(a_event->actorDying->GetFormID());
                }
                return RE::BSEventNotifyControl::kContinue;
            }

            RE::BSEventNotifyControl ProcessEvent(const RE::TESCellAttachDetachEvent* a_event,
                                                  RE::BSTEventSource<RE::TESCellAttachDetachEvent>*) override
            {
                if (a_event && a_event->reference && a_event->reference->IsDynamicForm()) {
                    Garrison::Get().OnActorAttach(a_event->reference->GetFormID(), a_event->attached);
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };
    }

    Garrison& Garrison::Get() noexcept
    {
        static Garrison instance;
        return instance;
    }

    // ---- Dados ------------------------------------------------------------------------------------

    void Garrison::Load()
    {
        ScopedTimer timer{ "Garrison::Load" };
        {
            std::scoped_lock l{ lock_ };
            // Padrões (Garrison.json sobrescreve).
            initial_ = { 2, 4, 2, 2, 3, 6, 2 };  // unknown, fort, tower, camp, village, city, claim
            max_     = { 4, 8, 4, 4, 6, 12, 3 };
            upkeep_.clear();
            troops_.clear();

            std::vector<fs::path> files{ kRoot / "Garrison.json" };
            std::vector<fs::path> extra;
            std::error_code       ec;
            for (const auto& entry : fs::directory_iterator(kRoot / "Garrison", ec)) {
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
                    logger::error("Guarnição: erro em {}: {}", f.filename().string(), e.what());
                }
            }
            playerFaction_ = RE::TESForm::LookupByID<RE::TESFaction>(kPlayerFaction);
            if (!playerFaction_) {
                logger::warn("Guarnição: PlayerFaction (0x{:08X}) não encontrada; soldados do jogador sem facção aliada.",
                             kPlayerFaction);
            }
            const auto& s = Config::Get().Garrison();
            logger::info("Guarnição: {} tropa(s), {} custo(s) por soldado, reposição {}/h, deserção {}/h após {} h de "
                         "déficit; atores {} (teto {}, {} por frame, raio {}).",
                         troops_.size(), upkeep_.size(), reinforcePerHour_, desertPerHour_, deficitGraceHours_,
                         s.enabled ? "ligados" : "desligados", s.maxActors, s.spawnPerFrame, s.spawnRadius);
        }

        auto& eco = Economy::Get();
        eco.AddTickListener("garrison", [](std::uint32_t a_hours) { Garrison::Get().OnHours(a_hours); });
        // Manutenção: chamado DENTRO do tick da economia (lock dela travado) — só lê State/Factions/Garrison.
        eco.RegisterUpkeep("garrison", [this](std::vector<Economy::Upkeep>& a_out) {
            if (upkeep_.empty()) {
                return;
            }
            std::unordered_map<Id, bool>   player;
            std::unordered_map<Id, double> total;
            for (const auto& f : Factions::Get().All()) {
                player[f.id] = f.player;
            }
            const auto& state = State::Get();
            Config::Get().ForEachTerritory([&](const Territory& a_t) {
                const auto owner = state.Owner(a_t.id);
                if (owner == kNeutral || (!player[owner] && !aiUpkeep_)) {
                    return;
                }
                std::scoped_lock l{ lock_ };
                total[owner] += std::floor(SizeUnlocked(a_t, owner));
            });
            for (const auto& [faction, soldiers] : total) {
                if (soldiers <= 0.0) {
                    continue;
                }
                for (const auto& [resource, perSoldier] : upkeep_) {
                    a_out.push_back({ faction, resource, perSoldier * soldiers });
                }
            }
        });

        Conquest::Get().AddListener(
            [](const Territory& a_territory, Id, Conquest::Source) { Garrison::Get().OnOwnerChanged(a_territory); });

        static bool registered = false;
        if (!registered) {
            if (const auto holder = RE::ScriptEventSourceHolder::GetSingleton()) {
                holder->AddEventSink<RE::TESCellFullyLoadedEvent>(WorldSink::Get());
                holder->AddEventSink<RE::TESActorLocationChangeEvent>(WorldSink::Get());
                holder->AddEventSink<RE::TESDeathEvent>(WorldSink::Get());
                holder->AddEventSink<RE::TESCellAttachDetachEvent>(WorldSink::Get());
            }
            registered = true;
        }
    }

    void Garrison::LoadFile(const fs::path& a_file)
    {
        const auto json = ReadJson(a_file);
        if (!json || !json->is_object()) {
            if (a_file.filename() == "Garrison.json") {
                logger::warn("Guarnição: {} ausente ou inválido; usando padrões e sem tropas.", a_file.string());
            }
            return;
        }
        const auto name = a_file.filename().string();
        const auto perType = [&](const char* a_field, std::array<std::uint32_t, kTypeCount>& a_out) {
            const auto it = json->find(a_field);
            if (it == json->end() || !it->is_object()) {
                return;
            }
            for (const auto& [typeKey, value] : it->items()) {
                const auto type = typeKey == "unknown" ? std::optional{ TerritoryType::kUnknown }
                                                       : ParseTerritoryType(typeKey);
                if (!type || !value.is_number_unsigned()) {
                    logger::warn("{}: {}.{} inválido; ignorado.", name, a_field, typeKey);
                    continue;
                }
                a_out[static_cast<std::size_t>(*type)] = value.get<std::uint32_t>();
            }
        };
        perType("initial", initial_);
        perType("max", max_);
        strengthPerSoldier_ = json->value("strengthPerSoldier", strengthPerSoldier_);
        reinforcePerHour_   = json->value("reinforcePerHour", reinforcePerHour_);
        desertPerHour_      = json->value("desertPerHour", desertPerHour_);
        deficitGraceHours_  = json->value("deficitGraceHours", deficitGraceHours_);
        aiUpkeep_           = json->value("aiUpkeep", aiUpkeep_);

        if (const auto u = json->find("upkeep"); u != json->end() && u->is_object()) {
            for (const auto& [resKey, value] : u->items()) {
                const auto id = MakeId(resKey);
                if (!value.is_number() || !Economy::Get().FindResource(id)) {
                    logger::warn("{}: upkeep.{}: recurso desconhecido em Economy.json; ignorado.", name, resKey);
                    continue;
                }
                std::erase_if(upkeep_, [&](const auto& p) { return p.first == id; });
                if (const auto v = value.get<double>(); v > 0.0) {
                    upkeep_.emplace_back(id, v);
                }
            }
        }
        if (const auto t = json->find("troops"); t != json->end() && t->is_object()) {
            for (const auto& [key, value] : t->items()) {
                if (value.is_object()) {
                    ResolveTroop(key, value);
                }
            }
        }
    }

    void Garrison::ResolveTroop(const std::string& a_key, const nlohmann::json& a_json)
    {
        Troop tr;
        tr.key = a_key;
        for (const auto& c : Candidates(a_json, "actor")) {
            const auto form = ResolveForm(c);
            if (form && (form->Is(RE::FormType::NPC) || form->Is(RE::FormType::LeveledNPC))) {
                tr.actor = static_cast<RE::TESBoundObject*>(form);
                logger::info("Guarnição: tropa '{}': ator '{}' -> 0x{:08X} ('{}').", a_key, c, form->GetFormID(),
                             form->GetName());
                break;
            }
            logger::info("Guarnição: tropa '{}': ator '{}' não encontrado (plugin ausente / EditorID sem po3 Tweaks / "
                         "não é NPC nem leveled list).",
                         a_key, c);
        }
        if (!tr.actor) {
            const auto hint = Lower(a_json.value("autoArmorHint", std::string{}));
            if (const auto npc = AutoSoldier(hint)) {
                tr.actor = npc;
                logger::info("Guarnição: tropa '{}': escolha automática (armadura '{}') -> 0x{:08X} ('{}').", a_key,
                             hint, npc->GetFormID(), npc->GetName());
            }
        }
        if (!tr.actor) {
            logger::warn("Guarnição: tropa '{}' sem ator válido; territórios desse estilo ficam só com números.", a_key);
            return;
        }
        tr.outfit = ResolveFirst<RE::BGSOutfit>(Candidates(a_json, "outfit"), RE::FormType::Outfit, "outfit", a_key);
        for (const auto& c : Candidates(a_json, "factions")) {
            if (const auto f = ResolveFirst<RE::TESFaction>({ c }, RE::FormType::Faction, "facção", a_key)) {
                tr.addFactions.push_back(f);
            }
        }
        for (const auto& c : Candidates(a_json, "removeFactions")) {
            if (const auto f = ResolveFirst<RE::TESFaction>({ c }, RE::FormType::Faction, "facção removida", a_key)) {
                tr.removeFactions.push_back(f);
            }
        }
        troops_[MakeId(a_key)] = std::move(tr);
    }

    const Garrison::Troop* Garrison::TroopFor(Id a_owner) const
    {
        // Facção dona -> estilo dela -> "default".
        if (const auto it = troops_.find(a_owner); it != troops_.end()) {
            return &it->second;
        }
        if (const auto it = troops_.find(Factions::Get().StyleOf(a_owner)); it != troops_.end()) {
            return &it->second;
        }
        const auto it = troops_.find(MakeId("default"));
        return it == troops_.end() ? nullptr : &it->second;
    }

    // ---- Números ------------------------------------------------------------------------------------

    std::uint32_t Garrison::Initial(TerritoryType a_type) const
    {
        const auto i = static_cast<std::size_t>(a_type);
        return i < kTypeCount ? initial_[i] : 0;
    }

    std::uint32_t Garrison::MaxOf(const Territory& a_territory) const
    {
        const auto i    = static_cast<std::size_t>(a_territory.type);
        const auto base = i < kTypeCount ? static_cast<double>(max_[i]) : 0.0;
        const auto up   = Upgrades::Get().Stat(a_territory.id, Stat::kGarrisonMax);  // M15
        return static_cast<std::uint32_t>(std::max(0.0, std::floor(base + up)));
    }

    double Garrison::SizeUnlocked(const Territory& a_territory, Id a_owner) const
    {
        if (a_owner == kNeutral) {
            return 0.0;
        }
        if (const auto it = entries_.find(a_territory.id); it != entries_.end() && it->second.owner == a_owner) {
            return it->second.soldiers;
        }
        return std::min(Initial(a_territory.type), MaxOf(a_territory));  // sem registro: guarnição inicial
    }

    Garrison::Entry& Garrison::EntryOf(const Territory& a_territory, Id a_owner)
    {
        auto& e = entries_[a_territory.id];
        if (e.owner != a_owner) {
            e.owner    = a_owner;
            e.soldiers = a_owner == kNeutral ? 0.0 : std::min(Initial(a_territory.type), MaxOf(a_territory));
        }
        return e;
    }

    std::uint32_t Garrison::Size(Id a_territory) const
    {
        const auto t = Config::Get().Find(a_territory);
        if (!t) {
            return 0;
        }
        const auto       owner = State::Get().Owner(a_territory);
        std::scoped_lock l{ lock_ };
        return static_cast<std::uint32_t>(std::floor(SizeUnlocked(*t, owner)));
    }

    std::uint32_t Garrison::Max(Id a_territory) const
    {
        const auto t = Config::Get().Find(a_territory);
        return t ? MaxOf(*t) : 0;
    }

    double Garrison::Strength(Id a_territory) const
    {
        auto&      up      = Upgrades::Get();
        const auto defense = 1.0 + up.Stat(a_territory, Stat::kDefense) / 100.0;
        const auto works   = up.InConstruction(a_territory) ? up.ConstructionDefenseMultiplier() : 1.0;
        return Size(a_territory) * strengthPerSoldier_ * std::max(0.0, defense) * works;
    }

    std::uint32_t Garrison::Present(Id a_territory) const
    {
        std::scoped_lock l{ lock_ };
        std::uint32_t    n = 0;
        for (const auto& [id, live] : live_) {
            if (live.territory == a_territory && !live.dead) {
                ++n;
            }
        }
        return n;
    }

    void Garrison::Set(Id a_territory, std::uint32_t a_soldiers)
    {
        const auto t = Config::Get().Find(a_territory);
        if (!t) {
            return;
        }
        const auto       owner = State::Get().Owner(a_territory);
        const auto       max   = MaxOf(*t);
        std::scoped_lock l{ lock_ };
        EntryOf(*t, owner).soldiers = owner == kNeutral ? 0.0 : std::min<double>(a_soldiers, max);
    }

    void Garrison::Add(Id a_territory, std::int32_t a_delta)
    {
        const auto t = Config::Get().Find(a_territory);
        if (!t) {
            return;
        }
        const auto       owner = State::Get().Owner(a_territory);
        const auto       max   = static_cast<double>(MaxOf(*t));
        std::scoped_lock l{ lock_ };
        auto&            e = EntryOf(*t, owner);
        e.soldiers         = owner == kNeutral ? 0.0 : std::clamp(e.soldiers + a_delta, 0.0, max);
    }

    void Garrison::OnHours(std::uint32_t a_hours)
    {
        ScopedTimer timer{ "Garrison::OnHours" };
        // Déficit da economia por facção (consultado antes de travar o lock da guarnição).
        std::unordered_map<Id, std::uint32_t> deficit;
        std::unordered_map<Id, bool>          player;
        for (const auto& f : Factions::Get().All()) {
            player[f.id]  = f.player;
            deficit[f.id] = Economy::Get().DeficitHours(f.id);
        }
        std::unordered_map<Id, double> deserted;
        const auto&                    state = State::Get();
        {
            std::scoped_lock l{ lock_ };
            Config::Get().ForEachTerritory([&](const Territory& a_t) {
                const auto owner = state.Owner(a_t.id);
                const auto it    = entries_.find(a_t.id);
                if (owner == kNeutral) {
                    if (it != entries_.end()) {
                        entries_.erase(it);
                    }
                    return;
                }
                // Territórios sem registro ficam na guarnição inicial, exceto os do jogador (pagam manutenção).
                if (it == entries_.end() && !player[owner] && !aiUpkeep_) {
                    return;
                }
                auto&      e   = EntryOf(a_t, owner);
                const auto max = static_cast<double>(MaxOf(a_t));
                const bool pays = !upkeep_.empty() && (player[owner] || aiUpkeep_);
                if (pays && deficit[owner] > deficitGraceHours_) {
                    const auto before = e.soldiers;
                    e.soldiers        = std::max(0.0, e.soldiers - desertPerHour_ * a_hours);
                    deserted[owner] += std::floor(before) - std::floor(e.soldiers);
                } else if (e.soldiers < max) {
                    e.soldiers = std::min(max, e.soldiers + reinforcePerHour_ * a_hours);
                } else {
                    e.soldiers = max;  // teto caiu (ex.: melhoria perdida)
                }
            });
        }
        for (const auto& [faction, n] : deserted) {
            if (n >= 1.0) {
                logger::info("Guarnição: {} soldado(s) de '{}' desertaram (déficit há {} h).", n,
                             Factions::Get().NameOf(faction), deficit[faction]);
                if (player[faction]) {
                    I18n::NotifyTr("garrison.desertion", static_cast<std::int64_t>(n));
                }
            }
        }
    }

    void Garrison::OnOwnerChanged(const Territory& a_territory)
    {
        const auto              owner = State::Get().Owner(a_territory.id);
        std::vector<RE::FormID> gone;
        {
            std::scoped_lock l{ lock_ };
            EntryOf(a_territory, owner);  // nova guarnição inicial da facção que tomou o local
            for (const auto& [id, live] : live_) {
                if (live.territory == a_territory.id && live.owner != owner) {
                    gone.push_back(id);
                }
            }
        }
        for (const auto id : gone) {
            Despawn(id);
        }
        logger::info("Guarnição: '{}' agora de '{}' com {} soldado(s).", a_territory.key,
                     Factions::Get().NameOf(owner), Size(a_territory.id));
        RequestRefresh();
    }

    // ---- Mundo: materialização perto do jogador -------------------------------------------------------

    RE::TESObjectREFR* Garrison::FindAnchor(const Territory& a_territory)
    {
        if (a_territory.location) {
            if (const auto marker = a_territory.location->worldLocMarker.get(); marker && CellAttached(marker.get())) {
                return marker.get();
            }
        }
        RE::TESObjectREFR* found = nullptr;
        for (const auto& set : a_territory.banners) {
            ForEachBanner(set, [&](RE::TESObjectREFR* a_ref) {
                if (!found && CellAttached(a_ref)) {
                    found = a_ref;
                }
            });
            if (found) {
                break;
            }
        }
        return found;
    }

    void Garrison::RequestRefresh()
    {
        if (!Config::Get().Garrison().enabled || refreshQueued_.exchange(true)) {
            return;
        }
        SKSE::GetTaskInterface()->AddTask([] {
            Garrison::Get().refreshQueued_ = false;
            Garrison::Get().Refresh();
        });
    }

    void Garrison::Refresh()
    {
        const auto& s      = Config::Get().Garrison();
        const auto  player = RE::PlayerCharacter::GetSingleton();
        if (!s.enabled || !player || !player->GetParentCell()) {
            return;
        }
        ScopedTimer timer{ "Garrison::Refresh" };
        const auto  ppos = player->GetPosition();
        const auto  ws   = player->GetWorldspace();
        const auto& state = State::Get();

        struct Active
        {
            const Territory* t;
            Id               owner;
            RE::FormID       anchor;
        };
        std::vector<Active> active;
        Config::Get().ForEachTerritory([&](const Territory& a_t) {
            const auto owner = state.Owner(a_t.id);
            if (owner == kNeutral) {
                return;
            }
            // Filtro barato primeiro (só matemática): mesmo worldspace e perto do jogador.
            if (a_t.hasPosition &&
                (!ws || a_t.worldspace != ws->GetFormID() || Distance2D(a_t.position, ppos) > s.spawnRadius)) {
                return;
            }
            const auto anchor = FindAnchor(a_t);
            if (anchor && Distance2D(anchor->GetPosition(), ppos) <= s.spawnRadius) {
                active.push_back({ &a_t, owner, anchor->GetFormID() });
            }
        });

        std::vector<RE::FormID> gone;
        std::vector<Job>        jobs;
        {
            std::scoped_lock l{ lock_ };
            // 1. Atores que não pertencem mais a um território ativo (longe, troca de dono, ref sumiu).
            for (const auto& [id, live] : live_) {
                const auto ref      = RE::TESForm::LookupByID<RE::Actor>(id);
                const bool isActive = std::any_of(active.begin(), active.end(),
                                                  [&](const Active& a) { return a.t->id == live.territory; });
                if (!ref || ref->IsDeleted() || state.Owner(live.territory) != live.owner ||
                    (!live.forced && !isActive)) {
                    gone.push_back(id);
                }
            }
            // 2. Territórios ativos: completa até o número da guarnição, dentro do teto global.
            std::uint32_t budget = s.maxActors > aliveTotal_ ? s.maxActors - aliveTotal_ : 0;
            for (const auto& q : queue_) {
                budget = budget > q.remaining ? budget - q.remaining : 0;
            }
            for (const auto& a : active) {
                if (budget == 0) {
                    break;
                }
                if (std::any_of(queue_.begin(), queue_.end(), [&](const Job& j) { return j.territory == a.t->id; })) {
                    continue;
                }
                std::uint32_t present = 0;
                for (const auto& [id, live] : live_) {
                    if (live.territory == a.t->id && !live.dead) {
                        ++present;
                    }
                }
                const auto size = static_cast<std::uint32_t>(std::floor(SizeUnlocked(*a.t, a.owner)));
                if (size > present) {
                    const auto n = std::min(size - present, budget);
                    budget -= n;
                    jobs.push_back({ a.t->id, a.owner, a.anchor, n, present, false });
                }
            }
        }
        for (const auto id : gone) {
            Despawn(id);
        }
        for (auto& j : jobs) {
            logger::info("Guarnição: {} soldado(s) a criar em '{}'.", j.remaining, Config::Get().NameOf(j.territory));
            Enqueue(j);
        }
    }

    void Garrison::Enqueue(Job a_job)
    {
        if (a_job.remaining == 0) {
            return;
        }
        {
            std::scoped_lock l{ lock_ };
            queue_.push_back(a_job);
        }
        if (!processing_.exchange(true)) {
            SKSE::GetTaskInterface()->AddTask([] { Garrison::Get().ProcessQueue(); });
        }
    }

    // Lote por frame: no máximo spawnPerFrame atores, depois reagenda para o próximo frame (AddTask).
    void Garrison::ProcessQueue()
    {
        auto budget = Config::Get().Garrison().spawnPerFrame;
        while (budget > 0) {
            Job job;
            {
                std::scoped_lock l{ lock_ };
                if (queue_.empty()) {
                    break;
                }
                job = queue_.front();
            }
            const bool ok = SpawnOne(job);
            {
                std::scoped_lock l{ lock_ };
                if (queue_.empty()) {
                    break;
                }
                auto& front = queue_.front();
                if (!ok || --front.remaining == 0) {
                    queue_.erase(queue_.begin());
                } else {
                    ++front.index;
                }
            }
            if (ok) {
                --budget;
            }
        }
        bool more;
        {
            std::scoped_lock l{ lock_ };
            more = !queue_.empty();
        }
        if (more) {
            SKSE::GetTaskInterface()->AddTask([] { Garrison::Get().ProcessQueue(); });
            return;
        }
        processing_ = false;
    }

    bool Garrison::SpawnOne(Job& a_job)
    {
        const auto& s      = Config::Get().Garrison();
        const auto  anchor = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_job.anchor);
        if (!anchor || !CellAttached(anchor)) {
            logger::info("Guarnição: âncora de '{}' não está mais carregada; lote cancelado.",
                         Config::Get().NameOf(a_job.territory));
            return false;
        }
        if (aliveTotal_.load() >= s.maxActors) {
            logger::info("Guarnição: teto global de {} ator(es) atingido.", s.maxActors);
            return false;
        }
        const Troop* troop = nullptr;
        {
            std::scoped_lock l{ lock_ };
            troop = TroopFor(a_job.owner);
        }
        if (!troop) {
            logger::warn("Guarnição: nenhuma tropa para '{}' (nem 'default'); só números.",
                         Factions::Get().NameOf(a_job.owner));
            return false;
        }
        const auto ref   = anchor->PlaceObjectAtMe(troop->actor, /*forcePersist*/ false);
        const auto actor = ref ? ref->As<RE::Actor>() : nullptr;
        if (!actor) {
            logger::error("Guarnição: PlaceObjectAtMe falhou para 0x{:08X}.", troop->actor->GetFormID());
            if (ref) {
                ref->Disable();
                ref->SetDelete(true);
            }
            return false;
        }
        // Círculo em volta do marcador (sem navmesh novo: o ator cai no chão/navmesh vanilla do local).
        const auto   theta  = kGoldenAngle * static_cast<float>(a_job.index);
        const auto   radius = std::max(96.0f, s.spreadRadius) * (0.6f + 0.2f * static_cast<float>(a_job.index % 3));
        RE::NiPoint3 pos    = anchor->GetPosition();
        pos.x += radius * std::cos(theta);
        pos.y += radius * std::sin(theta);
        pos.z += 32.0f;
        actor->SetPosition(pos, /*updateCharController*/ true);  // Actor::SetPosition oculta os overloads da base

        if (IsPlayerFaction(a_job.owner) && playerFaction_) {
            actor->AddToFaction(playerFaction_, 0);  // aliados do jogador (só nesta ref; nenhum registro editado)
        }
        for (const auto f : troop->removeFactions) {
            actor->RemoveFromFaction(f);
        }
        for (const auto f : troop->addFactions) {
            actor->AddToFaction(f, 0);
        }
        EnsureOutfit(actor, *troop);
        actor->EvaluatePackage();
        const auto id = actor->GetFormID();
        {
            std::scoped_lock l{ lock_ };
            live_[id] = { a_job.territory, a_job.owner, false, a_job.forced };
        }
        ++aliveTotal_;
        anyLive_ = true;
        // "Tropas peladas": o 3D pode ainda não existir; confere o outfit de novo no próximo frame.
        SKSE::GetTaskInterface()->AddTask([id, owner = a_job.owner] {
            const auto a = RE::TESForm::LookupByID<RE::Actor>(id);
            auto&      g = Garrison::Get();
            const Troop* t = nullptr;
            {
                std::scoped_lock l{ g.lock_ };
                t = g.TroopFor(owner);
            }
            if (a && t) {
                g.EnsureOutfit(a, *t);
            }
        });
        logger::info("Guarnição: soldado 0x{:08X} ('{}') criado em '{}' ({} vivo(s) no total).", id, actor->GetName(),
                     Config::Get().NameOf(a_job.territory), aliveTotal_.load());
        return true;
    }

    // Outfit aplicado pelo DLL (lição "tropas peladas"). Base temporária de leveled actor (0xFF......) pode receber o
    // outfit como padrão; base vanilla NUNCA é alterada: só os itens do outfit são vestidos nesta ref.
    void Garrison::EnsureOutfit(RE::Actor* a_actor, const Troop& a_troop)
    {
        const auto npc    = a_actor ? a_actor->GetActorBase() : nullptr;
        const auto outfit = a_troop.outfit ? a_troop.outfit : (npc ? npc->defaultOutfit : nullptr);
        if (!outfit || a_actor->IsDead()) {
            return;
        }
        if (a_troop.outfit && npc && npc->IsDynamicForm()) {
            a_actor->SetDefaultOutfit(outfit, true);
        }
        if (!a_actor->HasOutfitItems(outfit)) {
            a_actor->AddWornOutfit(outfit, true);
        }
    }

    void Garrison::Despawn(RE::FormID a_actor)
    {
        {
            std::scoped_lock l{ lock_ };
            const auto       it = live_.find(a_actor);
            if (it == live_.end()) {
                return;
            }
            if (!it->second.dead && aliveTotal_ > 0) {
                --aliveTotal_;
            }
            live_.erase(it);
            anyLive_ = !live_.empty();
        }
        if (const auto ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_actor)) {
            ref->Disable();
            ref->SetDelete(true);
        }
        logger::info("Guarnição: soldado 0x{:08X} removido do mundo.", a_actor);
    }

    void Garrison::OnActorDeath(RE::FormID a_actor)
    {
        if (!anyLive_.load()) {
            return;
        }
        Id territory = kNeutral;
        {
            std::scoped_lock l{ lock_ };
            const auto       it = live_.find(a_actor);
            if (it == live_.end() || it->second.dead) {
                return;
            }
            it->second.dead = true;
            territory       = it->second.territory;
            if (aliveTotal_ > 0) {
                --aliveTotal_;
            }
        }
        Add(territory, -1);  // a morte reduz o número da guarnição
        logger::info("Guarnição: soldado 0x{:08X} de '{}' morreu; restam {}.", a_actor, Config::Get().NameOf(territory),
                     Size(territory));
    }

    void Garrison::OnActorAttach(RE::FormID a_actor, bool a_attached)
    {
        if (!anyLive_.load()) {
            return;
        }
        Id owner = kNeutral;
        {
            std::scoped_lock l{ lock_ };
            const auto       it = live_.find(a_actor);
            if (it == live_.end()) {
                return;
            }
            owner = it->second.owner;
        }
        if (!a_attached) {
            // Célula descarregou: o soldado volta a ser só número.
            SKSE::GetTaskInterface()->AddTask([a_actor] { Garrison::Get().Despawn(a_actor); });
            return;
        }
        SKSE::GetTaskInterface()->AddTask([a_actor, owner] {
            auto&        g = Garrison::Get();
            const Troop* t = nullptr;
            {
                std::scoped_lock l{ g.lock_ };
                t = g.TroopFor(owner);
            }
            if (const auto a = RE::TESForm::LookupByID<RE::Actor>(a_actor); a && t) {
                g.EnsureOutfit(a, *t);  // confere o outfit ao carregar a célula
            }
        });
    }

    // ---- Debug --------------------------------------------------------------------------------------------

    void Garrison::DebugHere()
    {
        auto t = Conquest::Get().TerritoryAtPlayer(/*onlyCleared*/ false);
        if (!t) {
            if (const auto seat = Seat::Get().SeatTerritory(); seat != kNeutral) {
                t = Config::Get().Find(seat);
            } else {
                t = Config::Get().Find(Config::Get().Debug().territory);
            }
        }
        if (!t) {
            I18n::NotifyTr("garrison.noTerritory");
            return;
        }
        const auto owner = State::Get().Owner(t->id);
        if (owner == kNeutral) {
            I18n::NotifyTr("garrison.neutral", t->name);
            return;
        }
        const auto size    = Size(t->id);
        const auto present = Present(t->id);
        I18n::NotifyTr("garrison.status", t->name, Factions::Get().NameOf(owner), size, Max(t->id), present,
                       std::format("{:.1f}", Strength(t->id)));
        if (Economy::Get().InDeficit(owner)) {
            I18n::NotifyTr("garrison.deficit", Economy::Get().DeficitHours(owner));
        }
        const auto player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->GetParentCell()) {
            return;
        }
        bool hasTroop;
        {
            std::scoped_lock l{ lock_ };
            hasTroop = TroopFor(owner) != nullptr;
        }
        if (!hasTroop) {
            I18n::NotifyTr("garrison.noTroop", Factions::Get().NameOf(owner));
            return;
        }
        const auto& s     = Config::Get().Garrison();
        const auto  room  = s.maxActors > aliveTotal_ ? s.maxActors - aliveTotal_.load() : 0u;
        const auto  want  = size > present ? std::min(size - present, room) : 0u;
        if (want == 0) {
            I18n::NotifyTr(room == 0 ? "garrison.cap" : "garrison.allPresent", s.maxActors);
            return;
        }
        I18n::NotifyTr("garrison.spawning", want);
        Enqueue({ t->id, owner, player->GetFormID(), want, present, /*forced*/ true });
    }

    // ---- Cosave --------------------------------------------------------------------------------------------

    void Garrison::OnPostLoad()
    {
        // Atores de guarnição gravados no save (refs temporárias da sessão anterior): apaga e recria pelos números.
        std::vector<RE::FormID> stale;
        {
            std::scoped_lock l{ lock_ };
            stale.swap(stale_);
        }
        std::size_t removed = 0;
        for (const auto id : stale) {
            if (const auto ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(id); ref && !ref->IsDeleted()) {
                ref->Disable();
                ref->SetDelete(true);
                ++removed;
            }
        }
        if (removed > 0) {
            logger::info("Guarnição: {} ator(es) do save anterior removido(s).", removed);
        }
        RequestRefresh();
    }

    void Garrison::Clear()
    {
        std::scoped_lock l{ lock_ };
        entries_.clear();
        live_.clear();
        stale_.clear();
        queue_.clear();
        aliveTotal_ = 0;
        anyLive_    = false;
    }

    // Formato 'GARR' v1: u32 n × { Id território, Id dono, f64 soldados }, depois u32 m × { FormID ator vivo }.
    void Garrison::Save(SKSE::SerializationInterface* a_intfc) const
    {
        std::scoped_lock l{ lock_ };
        if (entries_.empty() && live_.empty()) {
            return;
        }
        if (!a_intfc->OpenRecord(kRecord, kRecordVersion)) {
            logger::error("Cosave: falha ao abrir o registro GARR.");
            return;
        }
        a_intfc->WriteRecordData(static_cast<std::uint32_t>(entries_.size()));
        for (const auto& [territory, e] : entries_) {
            a_intfc->WriteRecordData(territory);
            a_intfc->WriteRecordData(e.owner);
            a_intfc->WriteRecordData(e.soldiers);
        }
        a_intfc->WriteRecordData(static_cast<std::uint32_t>(live_.size()));
        for (const auto& [id, live] : live_) {
            a_intfc->WriteRecordData(id);
        }
    }

    void Garrison::LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version)
    {
        if (a_version != kRecordVersion) {
            logger::warn("Cosave: versão {} do registro GARR não suportada; guarnições recomeçam.", a_version);
            return;
        }
        std::scoped_lock l{ lock_ };
        std::uint32_t    count{};
        a_intfc->ReadRecordData(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            Id     territory{}, owner{};
            double soldiers{};
            a_intfc->ReadRecordData(territory);
            a_intfc->ReadRecordData(owner);
            a_intfc->ReadRecordData(soldiers);
            entries_[territory] = { owner, std::max(0.0, soldiers) };
        }
        std::uint32_t actors{};
        a_intfc->ReadRecordData(actors);
        for (std::uint32_t i = 0; i < actors; ++i) {
            RE::FormID oldId{}, newId{};
            a_intfc->ReadRecordData(oldId);
            if (a_intfc->ResolveFormID(oldId, newId)) {
                stale_.push_back(newId);
            }
        }
        logger::info("Cosave: guarnição de {} território(s), {} ator(es) antigo(s) a remover.", entries_.size(),
                     stale_.size());
    }
}
