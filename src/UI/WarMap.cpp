#include "UI/WarMap.h"

#include "Core/Factions.h"
#include "Core/I18n.h"
#include "Core/State.h"
#include "Core/Timer.h"
#include "Sim/Routes.h"
#include "Stock/Stock.h"
#include "UI/WarMapViews.h"
#include "World/Conquest.h"
#include "World/Seat.h"

#include <limits>

namespace AoS
{
    namespace
    {
        constexpr int   kSchemaVersion = 1;
        constexpr float kMax           = std::numeric_limits<float>::max();

        // Ids vão para o JS como texto hex de 8 dígitos (chave estável, sem problema de número no JSON).
        [[nodiscard]] std::string Hex(Id a_id) { return std::format("{:08X}", a_id); }

        [[nodiscard]] nlohmann::json HexOrNull(Id a_id)
        {
            return a_id == kNeutral ? nlohmann::json(nullptr) : nlohmann::json(Hex(a_id));
        }

        // Campo a_key de a_obj com um id hex ("0001ABCD"); nullopt se falta ou é inválido.
        [[nodiscard]] std::optional<Id> ParseHex(const nlohmann::json& a_obj, const char* a_key)
        {
            const auto it = a_obj.find(a_key);
            if (it == a_obj.end() || !it->is_string()) {
                return std::nullopt;
            }
            const auto& s = it->get_ref<const std::string&>();
            Id          v{};
            const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v, 16);
            return ec == std::errc{} && ptr == s.data() + s.size() ? std::optional<Id>{ v } : std::nullopt;
        }

        [[nodiscard]] std::string ColorHex(std::uint32_t a_rgb) { return std::format("#{:06X}", a_rgb & 0xFFFFFF); }

        [[nodiscard]] std::string_view RouteResultKey(Routes::Result a_result)
        {
            switch (a_result) {
            case Routes::Result::kOk:               return "route.ok";
            case Routes::Result::kSameTerritory:    return "route.same";
            case Routes::Result::kNoPosition:       return "route.noPosition";
            case Routes::Result::kOtherWorld:       return "route.otherWorld";
            case Routes::Result::kCannotTrade:      return "route.cannotTrade";
            case Routes::Result::kDuplicate:        return "route.duplicate";
            case Routes::Result::kLimit:            return "route.limit";
            case Routes::Result::kTooFar:           return "route.tooFar";
            default:                                return "route.unknown";
            }
        }

        [[nodiscard]] Id PlayerFactionId()
        {
            const auto f = Factions::Get().PlayerFaction();
            return f ? f->id : kNeutral;
        }

        [[nodiscard]] std::string WorldspaceName(RE::FormID a_id)
        {
            const auto ws   = RE::TESForm::LookupByID<RE::TESWorldSpace>(a_id);
            const auto name = ws ? ws->GetFullName() : nullptr;
            return name && *name ? std::string(name) : std::format("{:08X}", a_id);
        }
    }

    WarMap& WarMap::Get() noexcept
    {
        static WarMap instance;
        return instance;
    }

    void WarMap::Load()
    {
        ScopedTimer timer{ "WarMap::Load" };
        const auto& s = Config::Get().WarMap();
        if (!s.enabled) {
            logger::info("Mapa tático: desligado (settings.warMap.enabled = false); a mesa só notifica.");
            return;
        }
        const bool prisma = s.usePrismaUI && WarMapPrisma::Init();
        prisma_.store(prisma, std::memory_order_relaxed);
        logger::info("Mapa tático: {}.", prisma ? "PrismaUI detectado, usando a view HTML" :
                                                  (s.usePrismaUI ? "PrismaUI ausente, usando menus vanilla" :
                                                                   "menus vanilla (usePrismaUI = false)"));
        if (!loaded_) {
            loaded_ = true;
            // Mudou o dono por conquista com a view aberta (raro: a mesa pausa o jogo) -> reenvia.
            Conquest::Get().AddListener([](const Territory&, Id, Conquest::Source) { WarMap::Get().NotifyChanged(); });
        }
    }

    // ---- abrir / fechar ----------------------------------------------------------------------------------

    void WarMap::Open()
    {
        const auto& s = Config::Get().WarMap();
        if (!s.enabled) {
            const auto* t = Config::Get().Find(Seat::Get().SeatTerritory());
            I18n::NotifyTr("seat.warTable", t ? t->name : "?");
            return;
        }
        if (UsingPrismaUI()) {
            if (WarMapPrisma::Open()) {
                PushSnapshot();
                return;
            }
            logger::warn("Mapa tático: a view do PrismaUI não abriu ('{}'); usando menus vanilla.", s.view);
        }
        if (!WarMapMenus::Open()) {
            I18n::NotifyTr("warmap.unavailable");
        }
    }

    void WarMap::Close()
    {
        if (UsingPrismaUI()) {
            WarMapPrisma::Close();
        }
    }

    bool WarMap::IsOpen() const { return UsingPrismaUI() && WarMapPrisma::IsOpen(); }

    void WarMap::NotifyChanged()
    {
        if (!IsOpen() || refreshQueued_.exchange(true)) {
            return;
        }
        SKSE::GetTaskInterface()->AddTask([] {
            auto& map = WarMap::Get();
            map.refreshQueued_.store(false);
            map.PushSnapshot();
        });
    }

    void WarMap::PushSnapshot()
    {
        if (IsOpen()) {
            WarMapPrisma::PushData(BuildSnapshot());
        }
    }

    // ---- camadas ------------------------------------------------------------------------------------------

    void WarMap::AddProvider(std::string_view a_key, std::string_view a_labelKey, Provider a_provider)
    {
        {
            std::scoped_lock l{ lock_ };
            std::erase_if(layers_, [&](const Layer& x) { return x.key == a_key; });
            layers_.push_back({ std::string(a_key), std::string(a_labelKey), std::move(a_provider) });
        }
        logger::info("Mapa tático: camada '{}' registrada.", a_key);
        NotifyChanged();
    }

    void WarMap::RemoveProvider(std::string_view a_key)
    {
        std::scoped_lock l{ lock_ };
        std::erase_if(layers_, [&](const Layer& x) { return x.key == a_key; });
    }

    std::vector<std::pair<std::string, nlohmann::json>> WarMap::LayersOf(const Territory& a_territory) const
    {
        std::vector<Layer> layers;
        {
            std::scoped_lock l{ lock_ };
            layers = layers_;
        }
        std::vector<std::pair<std::string, nlohmann::json>> out;
        for (const auto& layer : layers) {
            auto v = layer.provider ? layer.provider(a_territory) : nlohmann::json{};
            if (!v.is_null()) {
                out.emplace_back(I18n::Tr(layer.labelKey.empty() ? layer.key : layer.labelKey), std::move(v));
            }
        }
        return out;
    }

    // ---- alvos --------------------------------------------------------------------------------------------

    bool WarMap::IsTarget(Id a_territory) const
    {
        std::scoped_lock l{ lock_ };
        return std::ranges::find(targets_, a_territory) != targets_.end();
    }

    std::vector<Id> WarMap::Targets() const
    {
        std::scoped_lock l{ lock_ };
        return targets_;
    }

    // ---- ações --------------------------------------------------------------------------------------------

    WarMap::ActionResult WarMap::CreateRoute(Id a_territoryA, Id a_territoryB)
    {
        // A mesa é do jogador: só liga territórios da facção dele (Routes::Create aceitaria qualquer par que
        // possa comerciar, inclusive de duas IAs).
        const auto player = PlayerFactionId();
        const auto& state = State::Get();
        if (player == kNeutral || state.Owner(a_territoryA) != player || state.Owner(a_territoryB) != player) {
            return { false, I18n::Tr("warmap.routeNotYours") };
        }
        const auto result = Routes::Get().Create(a_territoryA, a_territoryB);
        NotifyChanged();
        return { result == Routes::Result::kOk, I18n::Tr(RouteResultKey(result)) };
    }

    WarMap::ActionResult WarMap::RemoveRoute(Id a_route)
    {
        const auto status = Routes::Get().StatusOf(a_route);
        if (!status) {
            return { false, I18n::Tr("warmap.routeMissing") };
        }
        if (status->owner != PlayerFactionId() || status->owner == kNeutral) {
            return { false, I18n::Tr("warmap.routeNotYours") };
        }
        const bool ok = Routes::Get().Remove(a_route);
        NotifyChanged();
        return { ok, I18n::Tr(ok ? "warmap.routeRemoved" : "warmap.routeMissing") };
    }

    WarMap::ActionResult WarMap::SetTarget(Id a_territory, bool a_on)
    {
        const auto* t = Config::Get().Find(a_territory);
        if (!t) {
            return { false, I18n::Tr("route.unknown") };
        }
        if (const auto player = PlayerFactionId();
            a_on && player != kNeutral && State::Get().Owner(a_territory) == player) {
            return { false, I18n::Tr("warmap.targetOwn", t->name) };
        }
        {
            std::scoped_lock l{ lock_ };
            const auto       it = std::ranges::find(targets_, a_territory);
            if (a_on == (it != targets_.end())) {
                return { true, I18n::Tr(a_on ? "warmap.targetAdded" : "warmap.targetRemoved", t->name) };
            }
            if (a_on) {
                if (targets_.size() >= Config::Get().WarMap().maxTargets) {
                    return { false, I18n::Tr("warmap.targetLimit", Config::Get().WarMap().maxTargets) };
                }
                targets_.push_back(a_territory);
            } else {
                targets_.erase(it);
            }
        }
        logger::info("Mapa tático: alvo '{}' {}.", t->key, a_on ? "marcado" : "desmarcado");
        NotifyChanged();
        return { true, I18n::Tr(a_on ? "warmap.targetAdded" : "warmap.targetRemoved", t->name) };
    }

    void WarMap::HandleViewMessage(std::string a_message)
    {
        // O callback do PrismaUI pode vir de outra thread (a doc não diz): tudo vai para a thread principal.
        SKSE::GetTaskInterface()->AddTask([msg = std::move(a_message)] {
            auto&      map  = WarMap::Get();
            const auto json = nlohmann::json::parse(msg, nullptr, false);
            if (json.is_discarded() || !json.is_object()) {
                logger::warn("Mapa tático: mensagem inválida da view: {}", msg.substr(0, 200));
                return;
            }
            const auto action = json.value("action", std::string{});
            if (action == "close") {
                map.Close();
                return;
            }
            if (action == "ready") {  // a view (re)carregou: manda o snapshot de novo
                map.PushSnapshot();
                return;
            }
            std::optional<ActionResult> result;
            if (action == "createRoute") {
                const auto a = ParseHex(json, "a");
                const auto b = ParseHex(json, "b");
                result       = a && b ? map.CreateRoute(*a, *b) : ActionResult{ false, I18n::Tr("route.unknown") };
            } else if (action == "removeRoute") {
                const auto r = ParseHex(json, "route");
                result       = r ? map.RemoveRoute(*r) : ActionResult{ false, I18n::Tr("warmap.routeMissing") };
            } else if (action == "target") {
                const auto t = ParseHex(json, "territory");
                result       = t ? map.SetTarget(*t, json.value("on", true)) :
                                   ActionResult{ false, I18n::Tr("route.unknown") };
            } else {
                logger::warn("Mapa tático: ação desconhecida '{}'.", action);
                return;
            }
            logger::info("Mapa tático: ação '{}' -> {} ({}).", action, result->ok ? "ok" : "falhou", result->text);
            WarMapPrisma::PushResult(nlohmann::json{ { "ok", result->ok }, { "text", result->text } }.dump());
        });
    }

    // ---- snapshot -----------------------------------------------------------------------------------------

    std::string WarMap::BuildSnapshot() const
    {
        ScopedTimer timer{ "WarMap::BuildSnapshot" };
        const auto  start  = std::chrono::steady_clock::now();
        const auto& cfg    = Config::Get();
        const auto  player = PlayerFactionId();
        const auto  seatId = Seat::Get().SeatTerritory();

        std::vector<Layer> layers;
        std::vector<Id>    targets;
        {
            std::scoped_lock l{ lock_ };
            layers  = layers_;
            targets = targets_;
        }
        const std::unordered_set<Id> targetSet(targets.begin(), targets.end());

        nlohmann::json root;
        root["schema"] = kSchemaVersion;
        root["lang"]   = I18n::Language();

        // Textos da view: todas as chaves "warmap.*" + "route.*" (resultado das ações já vem traduzido).
        auto& i18n = root["i18n"] = nlohmann::json::object();
        for (auto& [key, value] : I18n::WithPrefix("warmap.")) {
            i18n[key] = std::move(value);
        }

        // Facções (cor para o mapa).
        auto& factions = root["factions"] = nlohmann::json::array();
        for (const auto& f : Factions::Get().All()) {
            factions.push_back({ { "id", Hex(f.id) }, { "name", f.name }, { "color", ColorHex(f.color) },
                                 { "player", f.player } });
        }
        root["playerFaction"] = HexOrNull(player);

        // Territórios (+ províncias e limites por worldspace na mesma passada).
        struct Bounds
        {
            float         minX{ kMax }, minY{ kMax }, maxX{ -kMax }, maxY{ -kMax };
            std::uint32_t count{ 0 };
        };
        std::unordered_map<RE::FormID, Bounds> bounds;
        std::unordered_map<Id, std::string>    provinces;
        auto& territories = root["territories"] = nlohmann::json::array();
        const auto& state = State::Get();
        cfg.ForEachTerritory([&](const Territory& t) {
            nlohmann::json j;
            j["id"]       = Hex(t.id);
            j["key"]      = t.key;
            j["name"]     = t.name;
            j["type"]     = std::string(ToString(t.type));
            j["province"] = HexOrNull(t.province);
            j["owner"]    = HexOrNull(state.Owner(t.id));
            if (t.hasPosition) {
                j["x"]  = t.position.x;
                j["y"]  = t.position.y;
                j["ws"] = Hex(t.worldspace);
                auto& b = bounds[t.worldspace];
                b.minX  = std::min(b.minX, t.position.x);
                b.maxX  = std::max(b.maxX, t.position.x);
                b.minY  = std::min(b.minY, t.position.y);
                b.maxY  = std::max(b.maxY, t.position.y);
                ++b.count;
            } else {
                j["x"] = j["y"] = j["ws"] = nullptr;
            }
            auto& n = j["neighbors"] = nlohmann::json::array();
            for (const auto id : t.neighbors) {
                n.push_back(Hex(id));
            }
            j["seat"]   = t.id == seatId;
            j["target"] = targetSet.contains(t.id);
            auto& l     = j["layers"] = nlohmann::json::object();
            for (const auto& layer : layers) {
                if (layer.provider) {
                    if (auto v = layer.provider(t); !v.is_null()) {
                        l[layer.key] = std::move(v);
                    }
                }
            }
            if (t.province != kNeutral && !provinces.contains(t.province)) {
                provinces.emplace(t.province, t.provinceName);
            }
            territories.push_back(std::move(j));
        });

        auto& provs = root["provinces"] = nlohmann::json::array();
        for (const auto& [id, name] : provinces) {
            provs.push_back({ { "id", Hex(id) }, { "name", name } });
        }

        // Worldspace principal: o da sede, senão o com mais territórios.
        RE::FormID mainWs = 0;
        if (const auto* st = cfg.Find(seatId); st && st->hasPosition) {
            mainWs = st->worldspace;
        }
        auto& worlds = root["worldspaces"] = nlohmann::json::array();
        std::uint32_t best = 0;
        for (const auto& [ws, b] : bounds) {
            worlds.push_back({ { "id", Hex(ws) }, { "name", WorldspaceName(ws) }, { "minX", b.minX },
                               { "minY", b.minY }, { "maxX", b.maxX }, { "maxY", b.maxY }, { "count", b.count } });
            if (mainWs == 0 && b.count > best) {
                best = b.count;
            }
        }
        if (mainWs == 0) {
            for (const auto& [ws, b] : bounds) {
                if (b.count == best) {
                    mainWs = ws;
                    break;
                }
            }
        }
        root["worldspace"] = mainWs ? nlohmann::json(Hex(mainWs)) : nlohmann::json(nullptr);

        // Rotas (M13) com o status atual (ativa / cortada).
        auto& routes = root["routes"] = nlohmann::json::array();
        for (const auto& r : Routes::Get().List()) {
            const auto s = Routes::Get().StatusOf(r.id);
            routes.push_back({ { "id", Hex(r.id) },
                               { "a", Hex(r.a) },
                               { "b", Hex(r.b) },
                               { "owner", HexOrNull(s ? s->owner : kNeutral) },
                               { "active", s && s->active },
                               { "blocked", s && s->blocked },
                               { "blocker", HexOrNull(s ? s->blocker : kNeutral) },
                               { "distance", s ? s->distance : 0.0f },
                               { "goldPerHour", s ? s->goldPerHour : 0.0 } });
        }

        // Sede + estoque (última contagem do baú, M7).
        if (seatId != kNeutral) {
            nlohmann::json seat;
            seat["territory"] = Hex(seatId);
            auto& stock       = seat["stock"] = nlohmann::json::array();
            if (const auto totals = Seat::Get().StockTotals()) {
                const auto& cats = Stock::Get().Categories();
                for (std::size_t i = 0; i < cats.size() && i < totals->perCategory.size(); ++i) {
                    stock.push_back({ { "key", cats[i].key }, { "name", cats[i].name },
                                      { "count", totals->perCategory[i] } });
                }
                seat["unclassified"] = totals->unclassified;
            } else {
                seat["unclassified"] = 0;
            }
            root["seat"] = std::move(seat);
        } else {
            root["seat"] = nullptr;
        }

        // Jogador (só posição em exterior; interior = null).
        nlohmann::json playerPos = nullptr;
        if (const auto pc = RE::PlayerCharacter::GetSingleton()) {
            const auto cell = pc->GetParentCell();
            const auto ws   = pc->GetWorldspace();
            if (cell && !cell->IsInteriorCell() && ws) {
                const auto p = pc->GetPosition();
                playerPos    = { { "x", p.x }, { "y", p.y }, { "ws", Hex(ws->GetFormID()) } };
            }
        }
        root["player"] = std::move(playerPos);

        auto& layerList = root["layers"] = nlohmann::json::array();
        for (const auto& layer : layers) {
            layerList.push_back({ { "key", layer.key },
                                  { "label", I18n::Tr(layer.labelKey.empty() ? layer.key : layer.labelKey) } });
        }
        root["limits"] = { { "maxRoutes", Routes::Get().MaxRoutes() },
                           { "maxTargets", Config::Get().WarMap().maxTargets } };

        const std::chrono::duration<double, std::milli> ms = std::chrono::steady_clock::now() - start;
        root["buildMs"]                                     = ms.count();
        auto out = root.dump();
        logger::info("Mapa tático: snapshot com {} território(s), {} rota(s), {} camada(s), {} bytes.",
                     territories.size(), routes.size(), layers.size(), out.size());
        return out;
    }

    // ---- cosave -------------------------------------------------------------------------------------------

    void WarMap::Clear()
    {
        std::scoped_lock l{ lock_ };
        targets_.clear();
    }

    void WarMap::Save(SKSE::SerializationInterface* a_intfc) const
    {
        std::scoped_lock l{ lock_ };
        if (!a_intfc->OpenRecord(kRecord, kRecordVersion)) {
            logger::error("Cosave: falha ao abrir o registro WMAP.");
            return;
        }
        const auto count = static_cast<std::uint32_t>(targets_.size());
        a_intfc->WriteRecordData(count);
        for (const auto id : targets_) {
            a_intfc->WriteRecordData(id);
        }
    }

    void WarMap::LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version)
    {
        if (a_version != kRecordVersion) {
            logger::warn("Cosave: WMAP versão {} desconhecida; alvos ignorados.", a_version);
            return;
        }
        std::uint32_t count{};
        if (!a_intfc->ReadRecordData(count)) {
            return;
        }
        std::vector<Id> loaded;
        for (std::uint32_t i = 0; i < count; ++i) {
            Id id{};
            if (!a_intfc->ReadRecordData(id)) {
                break;
            }
            if (Config::Get().Find(id)) {  // território que sumiu (mod removido) é descartado
                loaded.push_back(id);
            }
        }
        logger::info("Cosave: {} alvo(s) de ataque carregado(s) ({} descartado(s)).", loaded.size(),
                     count - loaded.size());
        std::scoped_lock l{ lock_ };
        targets_ = std::move(loaded);
    }
}
