#include "Sim/Routes.h"

#include "Banner/BannerController.h"
#include "Core/Factions.h"
#include "Core/I18n.h"
#include "Core/State.h"
#include "Core/Timer.h"

namespace AoS
{
    namespace
    {
        [[nodiscard]] Id RouteId(Id a_a, Id a_b)
        {
            return MakeId(std::format("route/{:08X}/{:08X}", std::min(a_a, a_b), std::max(a_a, a_b)));
        }

        [[nodiscard]] float Distance2D(const RE::NiPoint3& a_a, const RE::NiPoint3& a_b)
        {
            const auto dx = a_a.x - a_b.x;
            const auto dy = a_a.y - a_b.y;
            return std::sqrt(dx * dx + dy * dy);
        }

        // Distância (XY) do ponto P ao segmento AB.
        [[nodiscard]] float PointSegment2D(const RE::NiPoint3& a_p, const RE::NiPoint3& a_a, const RE::NiPoint3& a_b)
        {
            const auto abx = a_b.x - a_a.x;
            const auto aby = a_b.y - a_a.y;
            const auto len = abx * abx + aby * aby;
            auto       t   = len > 0.0f ? ((a_p.x - a_a.x) * abx + (a_p.y - a_a.y) * aby) / len : 0.0f;
            t              = std::clamp(t, 0.0f, 1.0f);
            const auto cx  = a_a.x + abx * t - a_p.x;
            const auto cy  = a_a.y + aby * t - a_p.y;
            return std::sqrt(cx * cx + cy * cy);
        }

        [[nodiscard]] std::string_view ResultKey(Routes::Result a_result)
        {
            switch (a_result) {
            case Routes::Result::kOk:               return "route.ok";
            case Routes::Result::kUnknownTerritory: return "route.unknown";
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
    }

    bool CanTrade(Id a_factionA, Id a_factionB)
    {
        return a_factionA != kNeutral && a_factionA == a_factionB;
    }

    Routes& Routes::Get() noexcept
    {
        static Routes instance;
        return instance;
    }

    void Routes::Configure(const nlohmann::json& a_routes)
    {
        std::scoped_lock l{ lock_ };
        typeMultiplier_ = { { TerritoryType::kUnknown, 1.0 }, { TerritoryType::kFort, 0.75 },
                            { TerritoryType::kTower, 0.5 },   { TerritoryType::kCamp, 0.5 },
                            { TerritoryType::kVillage, 1.0 }, { TerritoryType::kCity, 2.0 } };
        if (!a_routes.is_object()) {
            return;
        }
        resource_        = MakeId(a_routes.value("resource", "gold"s));
        maxRoutes_       = a_routes.value("maxRoutes", maxRoutes_);
        baseGoldPerHour_ = a_routes.value("baseGoldPerHour", baseGoldPerHour_);
        goldPer10kUnits_ = a_routes.value("goldPer10000Units", goldPer10kUnits_);
        maxDistance_     = a_routes.value("maxDistance", maxDistance_);
        blockRadius_     = a_routes.value("blockRadius", blockRadius_);
        if (const auto m = a_routes.find("typeMultiplier"); m != a_routes.end() && m->is_object()) {
            for (const auto& [key, value] : m->items()) {
                const auto type = key == "unknown" ? std::optional{ TerritoryType::kUnknown } : ParseTerritoryType(key);
                if (type && value.is_number()) {
                    typeMultiplier_[*type] = value.get<double>();
                } else {
                    logger::warn("Economy.json: routes.typeMultiplier '{}' inválido; ignorado.", key);
                }
            }
        }
        logger::info("Rotas: limite {}, base {}/h, {}/10000 unidades, raio de bloqueio {}.", maxRoutes_,
                     baseGoldPerHour_, goldPer10kUnits_, blockRadius_);
    }

    Id Routes::ResourceKey() const
    {
        std::scoped_lock l{ lock_ };
        return resource_;
    }

    std::size_t Routes::MaxRoutes() const
    {
        std::scoped_lock l{ lock_ };
        return maxRoutes_;
    }

    double Routes::TypeMultiplier(TerritoryType a_type) const
    {
        const auto it = typeMultiplier_.find(a_type);
        return it == typeMultiplier_.end() ? 1.0 : it->second;
    }

    Routes::Result Routes::Create(Id a_territoryA, Id a_territoryB, Id* a_outRoute)
    {
        const auto& cfg = Config::Get();
        const auto* ta  = cfg.Find(a_territoryA);
        const auto* tb  = cfg.Find(a_territoryB);
        Result      result = Result::kOk;
        if (!ta || !tb) {
            result = Result::kUnknownTerritory;
        } else if (ta == tb) {
            result = Result::kSameTerritory;
        } else if (!ta->hasPosition || !tb->hasPosition) {
            result = Result::kNoPosition;
        } else if (ta->worldspace != tb->worldspace) {
            result = Result::kOtherWorld;
        } else if (!CanTrade(State::Get().Owner(ta->id), State::Get().Owner(tb->id))) {
            result = Result::kCannotTrade;
        }

        if (result == Result::kOk) {
            std::scoped_lock l{ lock_ };
            const auto       id = RouteId(ta->id, tb->id);
            if (std::any_of(routes_.begin(), routes_.end(), [&](const Route& r) { return r.id == id; })) {
                result = Result::kDuplicate;
            } else if (routes_.size() >= maxRoutes_) {
                result = Result::kLimit;
            } else if (maxDistance_ > 0.0f && Distance2D(ta->position, tb->position) > maxDistance_) {
                result = Result::kTooFar;
            } else {
                routes_.push_back({ id, ta->id, tb->id });
                if (a_outRoute) {
                    *a_outRoute = id;
                }
            }
        }

        logger::info("Rotas: criar {} <-> {}: {}.", cfg.NameOf(a_territoryA), cfg.NameOf(a_territoryB),
                     ResultKey(result));
        return result;
    }

    bool Routes::Remove(Id a_route)
    {
        std::scoped_lock l{ lock_ };
        const auto       before = routes_.size();
        std::erase_if(routes_, [&](const Route& r) { return r.id == a_route; });
        if (routes_.size() != before) {
            logger::info("Rotas: rota 0x{:08X} removida.", a_route);
            return true;
        }
        return false;
    }

    std::vector<Routes::Route> Routes::List() const
    {
        std::scoped_lock l{ lock_ };
        return routes_;
    }

    std::optional<Routes::Status> Routes::StatusOf(Id a_route) const
    {
        std::scoped_lock l{ lock_ };
        for (const auto& r : routes_) {
            if (r.id == a_route) {
                return Evaluate(r);
            }
        }
        return std::nullopt;
    }

    // Custo: O(territórios) por rota. Com o limite de rotas (dezenas) e centenas de territórios, poucos milhares
    // de contas por tick — bem abaixo de 1 ms, e só 1×/hora de jogo.
    Routes::Status Routes::Evaluate(const Route& a_route) const
    {
        Status      s;
        const auto& cfg   = Config::Get();
        const auto& state = State::Get();
        const auto* ta    = cfg.Find(a_route.a);
        const auto* tb    = cfg.Find(a_route.b);
        if (!ta || !tb || !ta->hasPosition || !tb->hasPosition) {
            return s;
        }
        s.owner    = state.Owner(ta->id);
        s.distance = Distance2D(ta->position, tb->position);
        s.active   = CanTrade(s.owner, state.Owner(tb->id));
        if (!s.active) {
            return s;
        }
        for (const auto& t : cfg.Territories()) {
            if (&t == ta || &t == tb || !t.hasPosition || t.worldspace != ta->worldspace) {
                continue;
            }
            const auto o = state.Owner(t.id);
            if (o == kNeutral || CanTrade(o, s.owner)) {
                continue;  // neutros e aliados não cortam
            }
            if (PointSegment2D(t.position, ta->position, tb->position) < blockRadius_) {
                s.blocked = true;
                s.blocker = t.id;
                return s;
            }
        }
        const auto mult = (TypeMultiplier(ta->type) + TypeMultiplier(tb->type)) * 0.5;
        s.goldPerHour   = (baseGoldPerHour_ + goldPer10kUnits_ * (static_cast<double>(s.distance) / 10000.0)) * mult;
        return s;
    }

    std::vector<Routes::Income> Routes::Accrue(std::uint32_t a_hours, Totals& a_totals) const
    {
        std::vector<Income> out;
        std::scoped_lock    l{ lock_ };
        for (const auto& r : routes_) {
            const auto s = Evaluate(r);
            if (!s.active) {
                continue;
            }
            if (s.blocked) {
                ++a_totals.blocked;
                continue;
            }
            ++a_totals.active;
            const auto gold = s.goldPerHour * a_hours;
            a_totals.gold += gold;
            const auto it = std::find_if(out.begin(), out.end(), [&](const Income& i) { return i.faction == s.owner; });
            if (it == out.end()) {
                out.push_back({ s.owner, gold });
            } else {
                it->gold += gold;
            }
        }
        return out;
    }

    void Routes::DebugCreateNearest()
    {
        const auto player = RE::PlayerCharacter::GetSingleton();
        const auto ws     = player ? player->GetWorldspace() : nullptr;
        if (!ws) {
            I18n::NotifyTr("route.debugInterior");
            return;
        }
        const auto& cfg = Config::Get();
        const auto  pos = player->GetPosition();

        std::vector<std::pair<float, const Territory*>> near;
        for (const auto& t : cfg.Territories()) {
            if (t.hasPosition && t.worldspace == ws->GetFormID()) {
                near.emplace_back(Distance2D(pos, t.position), &t);
            }
        }
        if (near.size() < 2) {
            I18n::NotifyTr("route.debugNone");
            return;
        }
        constexpr std::size_t kScan = 16;
        const auto            n     = std::min(kScan, near.size());
        std::partial_sort(near.begin(), near.begin() + static_cast<std::ptrdiff_t>(n), near.end(),
                          [](const auto& x, const auto& y) { return x.first < y.first; });

        // 1. Dupla mais próxima com o mesmo dono (não neutro).
        const auto& state = State::Get();
        const Territory* a = nullptr;
        const Territory* b = nullptr;
        for (std::size_t i = 0; i < n && !a; ++i) {
            for (std::size_t j = i + 1; j < n; ++j) {
                const auto o = state.Owner(near[i].second->id);
                if (o != kNeutral && CanTrade(o, state.Owner(near[j].second->id))) {
                    a = near[i].second;
                    b = near[j].second;
                    break;
                }
            }
        }

        // 2. Nenhuma: só para teste, os 2 mais próximos passam para a facção do jogador (cria uma se preciso).
        if (!a) {
            auto& factions = Factions::Get();
            auto  pf       = factions.PlayerFaction();
            if (!pf && !cfg.Styles().empty()) {
                if (const auto id = factions.CreatePlayerFaction(I18n::Tr("selftest.playerFactionName"),
                                                                 cfg.Styles().back(), 0x2E7D32)) {
                    pf = factions.Find(*id);
                }
            }
            if (!pf) {
                I18n::NotifyTr("route.debugNone");
                return;
            }
            a = near[0].second;
            b = near[1].second;
            SetTerritoryOwner(a->id, pf->id);
            SetTerritoryOwner(b->id, pf->id);
            logger::info("Rotas (debug): '{}' e '{}' passaram para '{}' para o teste.", a->key, b->key, pf->key);
        }

        Id         id{};
        const auto result = Create(a->id, b->id, &id);
        if (result == Result::kOk || result == Result::kDuplicate) {
            const auto s = StatusOf(id == kNeutral ? RouteId(a->id, b->id) : id);
            I18n::NotifyTr("route.debugCreated", a->name, b->name, static_cast<int>(s ? s->distance / 70.0f : 0.0f),
                           std::format("{:.2f}", s ? s->goldPerHour : 0.0));
            if (s && s->blocked) {
                I18n::NotifyTr("route.blocked", cfg.Find(s->blocker) ? cfg.Find(s->blocker)->name : "?"s);
            }
        } else {
            I18n::NotifyTr(ResultKey(result));
        }
    }

    void Routes::Clear()
    {
        std::scoped_lock l{ lock_ };
        routes_.clear();
    }

    void Routes::Save(SKSE::SerializationInterface* a_intfc) const
    {
        std::scoped_lock l{ lock_ };
        if (routes_.empty()) {
            return;
        }
        if (!a_intfc->OpenRecord(kRecord, kRecordVersion)) {
            logger::error("Cosave: falha ao abrir o registro ROUT.");
            return;
        }
        a_intfc->WriteRecordData(static_cast<std::uint32_t>(routes_.size()));
        for (const auto& r : routes_) {
            a_intfc->WriteRecordData(r.a);
            a_intfc->WriteRecordData(r.b);
        }
    }

    void Routes::LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version)
    {
        if (a_version != kRecordVersion) {
            logger::warn("Cosave: versão {} do registro ROUT não suportada; rotas descartadas.", a_version);
            return;
        }
        std::scoped_lock l{ lock_ };
        std::uint32_t    count{};
        a_intfc->ReadRecordData(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            Id a{}, b{};
            a_intfc->ReadRecordData(a);
            a_intfc->ReadRecordData(b);
            // Território sumiu (JSON/plugin removido): descarta a rota em vez de manter lixo.
            if (!Config::Get().Find(a) || !Config::Get().Find(b)) {
                logger::warn("Cosave: rota 0x{:08X} <-> 0x{:08X} com território inexistente; descartada.", a, b);
                continue;
            }
            routes_.push_back({ RouteId(a, b), a, b });
        }
        logger::info("Cosave: {} rota(s) carregada(s).", routes_.size());
    }
}
