#include "Sim/Ai.h"

#include "Banner/BannerController.h"
#include "Core/Factions.h"
#include "Core/I18n.h"
#include "Core/State.h"
#include "Core/Timer.h"
#include "Sim/Diplomacy.h"
#include "Sim/Economy.h"
#include "Sim/Garrison.h"  // M25: guarnição derrotada -> prisioneiros (Law::CaptureFromCombat)
#include "Sim/Law.h"
#include "Sim/Routes.h"
#include "World/Conquest.h"
#include "World/Discovery.h"
#include "World/Seat.h"

namespace AoS
{
    namespace
    {
        const fs::path kFactionDir = "Data/SKSE/Plugins/AgeOfSkyrim/Factions";

        [[nodiscard]] double HoursNow()
        {
            const auto cal = RE::Calendar::GetSingleton();
            return cal ? static_cast<double>(cal->GetHoursPassed()) : 0.0;
        }

        // Número pseudoaleatório em [0, 1) a partir de uma semente (splitmix64): reproduzível no log, sem estado.
        [[nodiscard]] double Unit(std::uint64_t a_seed)
        {
            std::uint64_t z = a_seed + 0x9E3779B97F4A7C15ull;
            z               = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z               = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            z ^= z >> 31;
            return static_cast<double>(z >> 11) * (1.0 / 9007199254740992.0);
        }

        [[nodiscard]] float Distance2D(const RE::NiPoint3& a_a, const RE::NiPoint3& a_b)
        {
            const auto dx = a_a.x - a_b.x;
            const auto dy = a_a.y - a_b.y;
            return std::sqrt(dx * dx + dy * dy);
        }

        [[nodiscard]] Id PlayerFactionId()
        {
            const auto f = Factions::Get().PlayerFaction();
            return f ? f->id : kNeutral;
        }

        [[nodiscard]] bool IsPlayerFaction(Id a_faction)
        {
            const auto f = a_faction != kNeutral ? Factions::Get().Find(a_faction) : std::nullopt;
            return f && f->player;
        }
    }

    Ai& Ai::Get() noexcept
    {
        static Ai instance;
        return instance;
    }

    // ---- Dados ------------------------------------------------------------------------------------

    void Ai::Load()
    {
        ScopedTimer timer{ "Ai::Load" };
        aiFactions_.clear();
        aggression_.clear();
        excluded_.clear();
        for (const auto& f : Factions::Get().All()) {
            if (!f.player) {
                aiFactions_.insert(f.id);
            }
        }

        // "aggression" e "start" ficam nas entradas de Factions/*.json (Factions::Load ignora as chaves extras).
        std::vector<std::pair<Id, nlohmann::json>> starts;
        std::error_code                            ec;
        std::vector<fs::path>                      files;
        for (const auto& entry : fs::directory_iterator(kFactionDir, ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                files.push_back(entry.path());
            }
        }
        std::sort(files.begin(), files.end());
        for (const auto& f : files) {
            try {
                LoadFactionFile(f, starts);
            } catch (const std::exception& e) {
                logger::error("IA: erro em {}: {}", f.filename().string(), e.what());
            }
        }
        ApplyStarts(starts);

        // Exclusões calculadas uma vez para os territórios estáticos (no tick vira um lookup O(1)).
        const auto& cfg = Config::Get().Ai();
        for (const auto& t : Config::Get().Territories()) {
            if ((cfg.respectConquestExclusions && Conquest::Get().IsExcluded(t)) ||
                (cfg.skipCivilWarForts && Discovery::IsMilitaryFort(t.location))) {
                excluded_.insert(t.id);
            }
        }
        logger::info("IA: {} facção(ões) de IA, {} território(s) fora do alcance (exclusões/Guerra Civil), tick a cada "
                     "{} h, {}.",
                     aiFactions_.size(), excluded_.size(), cfg.tickHours, cfg.enabled ? "ligada" : "desligada");

        static bool registered = false;
        if (!registered) {
            registered = true;
            Economy::Get().AddTickListener("ai", [](std::uint32_t a_hours) { Ai::Get().OnHours(a_hours); });
            // Sede retomada pelo jogador: o depósito da economia volta para o baú (ADR 0048).
            Conquest::Get().AddListener([](const Territory& a_t, Id, Conquest::Source) {
                auto& ai   = Ai::Get();
                auto& seat = Seat::Get();
                bool  was  = false;
                {
                    std::scoped_lock l{ ai.lock_ };
                    was                 = ai.depotSuspended_ && seat.SeatTerritory() == a_t.id;
                    ai.depotSuspended_ = ai.depotSuspended_ && !was;
                }
                if (was) {
                    Economy::Get().SetDepot(seat.Chest());
                    logger::info("IA: sede '{}' retomada; depósito religado.", a_t.key);
                    I18n::NotifyTr("ai.seatRecovered", a_t.name);
                }
            });
        }
    }

    // Formato: { "factions": [ { "id": "imperial", ..., "aggression": 0.3,
    //            "start": { "locations": ["Skyrim.esm|0x018A5A"], "province": true, "types": ["city", "fort"] } } ] }
    void Ai::LoadFactionFile(const fs::path& a_file, std::vector<std::pair<Id, nlohmann::json>>& a_starts)
    {
        std::ifstream in(a_file);
        if (!in) {
            return;
        }
        nlohmann::json json;
        try {
            json = nlohmann::json::parse(in, nullptr, true, /*ignore_comments*/ true);
        } catch (const std::exception&) {
            return;  // Factions::Load já avisou
        }
        for (const auto& jf : json.value("factions", nlohmann::json::array())) {
            if (!jf.is_object()) {
                continue;
            }
            const auto id = MakeId(jf.value("id", ""s));
            if (!aiFactions_.contains(id)) {
                continue;  // facção ignorada pelo Factions::Load (estilo inválido etc.)
            }
            if (const auto a = jf.find("aggression"); a != jf.end() && a->is_number()) {
                aggression_[id] = std::clamp(a->get<double>(), 0.0, 1.0);
            }
            if (const auto s = jf.find("start"); s != jf.end() && s->is_object()) {
                a_starts.emplace_back(id, *s);
            }
        }
    }

    // Donos iniciais (jogo novo): o local da lista e, com "province", os territórios dos tipos pedidos na mesma
    // província (Hold). Só preenche territórios sem defaultOwner no JSON. Local/plugin ausente = aviso.
    void Ai::ApplyStarts(const std::vector<std::pair<Id, nlohmann::json>>& a_starts)
    {
        auto& cfg = Config::Get();
        for (const auto& [faction, start] : a_starts) {
            std::vector<TerritoryType> types;
            for (const auto& v : start.value("types", nlohmann::json::array())) {
                if (const auto t = v.is_string() ? ParseTerritoryType(v.get<std::string>()) : std::nullopt) {
                    types.push_back(*t);
                }
            }
            const bool  province = start.value("province", false);
            std::size_t count    = 0;
            for (const auto& v : start.value("locations", nlohmann::json::array())) {
                const auto ref = v.is_string() ? FormRef::Parse(v.get<std::string>()) : std::nullopt;
                const auto loc = ref ? ref->Resolve<RE::BGSLocation>() : nullptr;
                const auto t   = loc ? cfg.FindByLocation(loc) : nullptr;
                if (!t) {
                    logger::warn("IA: '{}': local inicial {} não encontrado ou não é território; ignorado.",
                                 Factions::Get().NameOf(faction), v.is_string() ? v.get<std::string>() : "?"s);
                    continue;
                }
                cfg.SetDefaultOwner(t->id, faction);
                ++count;
                if (province && t->province != kNeutral) {
                    for (const auto& o : cfg.Territories()) {
                        if (&o != t && o.province == t->province &&
                            (types.empty() || std::find(types.begin(), types.end(), o.type) != types.end())) {
                            cfg.SetDefaultOwner(o.id, faction);
                            ++count;
                        }
                    }
                }
            }
            logger::info("IA: '{}' começa com até {} território(s) (sem dono no JSON).", Factions::Get().NameOf(faction),
                         count);
        }
    }

    // ---- Integração ---------------------------------------------------------------------------------

    void Ai::SetDefenseProvider(DefenseProvider a_provider)
    {
        std::scoped_lock l{ lock_ };
        defense_ = std::move(a_provider);
        logger::info("IA: provedor de defesa {}.", defense_ ? "registrado" : "padrão (por tipo)");
    }

    void Ai::SetNearPlayerHandler(NearPlayerHandler a_handler)
    {
        std::scoped_lock l{ lock_ };
        nearPlayer_ = std::move(a_handler);
    }

    double Ai::DefenseOf(const Territory& a_territory) const
    {
        return DefenseOf(a_territory, State::Get().Owner(a_territory.id));
    }

    double Ai::DefenseOf(const Territory& a_territory, Id a_owner) const
    {
        DefenseProvider provider;
        {
            std::scoped_lock l{ lock_ };
            provider = defense_;
        }
        if (provider) {
            return std::max(0.0, provider(a_territory, a_owner));
        }
        const auto& cfg  = Config::Get().Ai();
        const auto  it   = cfg.defenseByType.find(a_territory.type);
        const auto  base = it == cfg.defenseByType.end() ? 15.0 : it->second;
        return a_owner == kNeutral ? base * cfg.neutralDefenseMultiplier : base;
    }

    bool Ai::IsAiFaction(Id a_faction) const { return aiFactions_.contains(a_faction); }

    bool Ai::CanTarget(const Territory& a_territory) const
    {
        if (excluded_.contains(a_territory.id)) {
            return false;
        }
        if (a_territory.type == TerritoryType::kHouse) {  // M29: casa do jogador, nunca alvo da IA
            return false;
        }
        if (a_territory.type == TerritoryType::kClaim) {  // dinâmico: só a lista por id
            return !(Config::Get().Ai().respectConquestExclusions && Conquest::Get().IsExcluded(a_territory));
        }
        return true;
    }

    // ---- Consulta -------------------------------------------------------------------------------------

    std::vector<Ai::Attack> Ai::ActiveAttacks() const
    {
        std::scoped_lock l{ lock_ };
        return attacks_;
    }

    std::vector<Ai::Attack> Ai::Threats(Id a_territory) const
    {
        std::scoped_lock    l{ lock_ };
        std::vector<Attack> out;
        for (const auto& a : attacks_) {
            if (a.target == a_territory) {
                out.push_back(a);
            }
        }
        return out;
    }

    double Ai::HoursToNextTick() const
    {
        std::scoped_lock l{ lock_ };
        const auto       every = Config::Get().Ai().tickHours;
        return sinceTick_ >= every ? 0.0 : static_cast<double>(every - sinceTick_);
    }

    double Ai::Roll(Id a_attack) const
    {
        const auto r = Config::Get().Ai().randomness;
        const auto u = Unit((static_cast<std::uint64_t>(a_attack) << 32) ^ static_cast<std::uint64_t>(HoursNow()));
        return 1.0 + r * (2.0 * u - 1.0);
    }

    bool Ai::PlayerNear(const Territory& a_territory) const
    {
        const auto player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return false;
        }
        if (const auto loc = player->GetCurrentLocation(); loc && a_territory.location &&
                                                          (loc == a_territory.location || loc->IsParent(a_territory.location))) {
            return true;
        }
        const auto ws = player->GetWorldspace();
        return a_territory.hasPosition && ws && ws->GetFormID() == a_territory.worldspace &&
               Distance2D(player->GetPosition(), a_territory.position) <= Config::Get().Ai().nearPlayerRadius;
    }

    // ---- Tick ------------------------------------------------------------------------------------------

    void Ai::OnHours(std::uint32_t a_hours)
    {
        const auto& cfg = Config::Get().Ai();
        if (!cfg.enabled || a_hours == 0) {
            return;
        }
        ScopedTimer timer{ "Ai::OnHours" };
        Summary     s;
        Advance(a_hours, s);
        bool run = false;
        {
            std::scoped_lock l{ lock_ };
            sinceTick_ += a_hours;
            if (sinceTick_ >= cfg.tickHours) {
                sinceTick_ %= cfg.tickHours;  // catch-up longo: uma avaliação só (o resto é descartado)
                run = true;
            }
        }
        if (run) {
            const auto st = Strategic();
            logger::info("IA: tick estratégico ({} h): {} território(s) avaliado(s), {} ataque(s) lançado(s), {} "
                         "guerra(s) declarada(s); {} ataque(s) resolvido(s), {} captura(s) ({:.3f} ms).",
                         a_hours, st.evaluated, st.launched, st.declared, s.resolved, s.captured, st.ms);
        }
    }

    // Marchas avançam; as que chegaram são resolvidas (ou entregues ao gancho da M21).
    void Ai::Advance(std::uint32_t a_hours, Summary& a_summary)
    {
        std::vector<Attack> arrived;
        {
            std::scoped_lock l{ lock_ };
            for (auto& [faction, left] : cooldown_) {
                left = left > a_hours ? left - a_hours : 0;
            }
            std::erase_if(cooldown_, [](const auto& p) { return p.second == 0; });
            for (auto it = attacks_.begin(); it != attacks_.end();) {
                if (it->engaged) {
                    ++it;
                    continue;
                }
                it->hoursLeft = it->hoursLeft > a_hours ? it->hoursLeft - a_hours : 0;
                if (it->hoursLeft == 0) {
                    arrived.push_back(*it);
                    it = attacks_.erase(it);
                } else {
                    ++it;
                }
            }
        }
        const auto& cfg = Config::Get();
        for (auto& a : arrived) {
            const auto* t = cfg.Find(a.target);
            if (!t) {
                continue;
            }
            const auto owner = State::Get().Owner(a.target);
            const bool valid = owner != a.attacker &&
                               (owner == kNeutral ? cfg.Ai().expandIntoNeutral : Diplomacy::Get().AtWar(a.attacker, owner));
            if (!valid) {
                logger::info("IA: ataque de '{}' a '{}' cancelado (dono agora '{}', sem guerra).",
                             Factions::Get().NameOf(a.attacker), t->key, Factions::Get().NameOf(owner));
                continue;
            }
            a.defender = owner;
            if (IsPlayerFaction(owner) && PlayerNear(*t)) {
                NearPlayerHandler handler;
                {
                    std::scoped_lock l{ lock_ };
                    handler = nearPlayer_;
                }
                OnAttackArrivedNearPlayer(a);
                if (handler && handler(a)) {
                    a.engaged = true;  // M21 assume e chama ResolveAttack
                    std::scoped_lock l{ lock_ };
                    attacks_.push_back(a);
                    continue;
                }
            }
            const auto defense = DefenseOf(*t, owner);
            const auto roll    = a.strength * Roll(a.id);
            logger::info("IA: '{}' chega a '{}': força {:.1f} (sorteio {:.1f}) x defesa {:.1f}.",
                         Factions::Get().NameOf(a.attacker), t->key, a.strength, roll, defense);
            Resolve(a, roll > defense, &a_summary);
        }
    }

    void Ai::OnAttackArrivedNearPlayer(const Attack& a_attack)
    {
        const auto* t = Config::Get().Find(a_attack.target);
        logger::info("IA: ataque de '{}' a '{}' chegou com o jogador por perto. TODO(M21): batalha em tempo real.",
                     Factions::Get().NameOf(a_attack.attacker), t ? t->key : "?"s);
        I18n::NotifyTr("ai.underAttack", t ? t->name : "?"s, Factions::Get().NameOf(a_attack.attacker));
    }

    bool Ai::ResolveAttack(Id a_attack, bool a_attackerWon)
    {
        Attack found;
        {
            std::scoped_lock l{ lock_ };
            const auto       it = std::find_if(attacks_.begin(), attacks_.end(), [&](const Attack& a) { return a.id == a_attack; });
            if (it == attacks_.end()) {
                return false;
            }
            found = *it;
            attacks_.erase(it);
        }
        found.defender = State::Get().Owner(found.target);
        Resolve(found, a_attackerWon, nullptr);
        return true;
    }

    void Ai::Resolve(Attack a_attack, bool a_attackerWon, Summary* a_summary)
    {
        const auto* t = Config::Get().Find(a_attack.target);
        if (!t) {
            return;
        }
        auto&      factions = Factions::Get();
        const bool player   = IsPlayerFaction(a_attack.defender);
        if (a_summary) {
            ++a_summary->resolved;
        }
        if (!a_attackerWon) {
            logger::info("IA: '{}' defendeu '{}' contra '{}'.", factions.NameOf(a_attack.defender), t->key,
                         factions.NameOf(a_attack.attacker));
            if (player) {
                I18n::NotifyTr("ai.repelled", t->name, factions.NameOf(a_attack.attacker));
            }
            return;
        }
        // M25: a guarnição que o alvo tinha antes de cair vira a base dos prisioneiros do vencedor
        // (Law::CaptureFromCombat, que já aplica a fração/teto configurados). Lido ANTES de SetTerritoryOwner:
        // depois da troca, Garrison::Size já reflete o novo dono (guarnição inicial dele), não a derrota.
        const auto defeatedGarrison = Garrison::Get().Size(t->id);
        if (!SetTerritoryOwner(t->id, a_attack.attacker)) {
            return;
        }
        Law::Get().CaptureFromCombat(a_attack.attacker, a_attack.defender, defeatedGarrison);
        if (a_summary) {
            ++a_summary->captured;
        }
        logger::info("IA: '{}' tomou '{}' de '{}'.", factions.NameOf(a_attack.attacker), t->key,
                     factions.NameOf(a_attack.defender));
        if (!player) {
            return;
        }
        I18n::NotifyTr("ai.lost", t->name, factions.NameOf(a_attack.attacker));
        // Sede perdida (ADR 0048): baú e mesa ficam onde estão (itens reais do jogador não somem); a produção
        // volta a acumular como pendente até retomar o território ou mudar a sede.
        if (Seat::Get().SeatTerritory() == t->id) {
            {
                std::scoped_lock l{ lock_ };
                depotSuspended_ = true;
            }
            Economy::Get().SetDepot(nullptr);
            logger::info("IA: sede '{}' ocupada; depósito suspenso.", t->key);
            I18n::NotifyTr("ai.seatLost", t->name);
        }
    }

    // Avaliação estratégica: um lote de territórios a partir do cursor; no máximo um ataque novo por facção.
    Ai::Summary Ai::Strategic()
    {
        const auto  start = std::chrono::steady_clock::now();
        Summary     s;
        const auto& cfg   = Config::Get().Ai();
        const auto& state = State::Get();
        auto&       dipl  = Diplomacy::Get();
        auto&       econ  = Economy::Get();
        const auto  gold  = Routes::Get().ResourceKey();

        std::vector<const Territory*> all;
        Config::Get().ForEachTerritory([&](const Territory& a_t) { all.push_back(&a_t); });
        if (all.empty()) {
            return s;
        }

        // Estado atual (cópia curta sob o lock; o resto roda sem ele).
        std::unordered_map<Id, std::size_t> perFaction;
        std::unordered_set<Id>              targeted;
        std::unordered_set<Id>              cooling;
        std::size_t                         active = 0;
        std::size_t                         cursor = 0;
        {
            std::scoped_lock l{ lock_ };
            active = attacks_.size();
            for (const auto& a : attacks_) {
                ++perFaction[a.attacker];
                targeted.insert(a.target);
            }
            for (const auto& [f, left] : cooldown_) {
                cooling.insert(f);
            }
            cursor = cursor_ % all.size();
        }

        struct Candidate
        {
            const Territory* origin{ nullptr };
            const Territory* target{ nullptr };
            Id               defender{ kNeutral };
            double           gold{ 0.0 };
            double           strength{ 0.0 };
        };
        std::unordered_map<Id, Candidate>           best;     // facção -> alvo mais barato
        std::unordered_map<Id, double>              budget;   // cache do saldo de ouro
        std::vector<std::pair<Id, Id>>              peaceful; // (IA, vizinho em paz) para a agressão
        const auto budgetOf = [&](Id a_f) {
            const auto [it, inserted] = budget.try_emplace(a_f, 0.0);
            if (inserted) {
                it->second = econ.Balance(a_f, gold);
            }
            return it->second;
        };

        const auto consider = [&](const Territory* a_origin, const Territory* a_target) {
            if (!a_origin || !a_target || targeted.contains(a_target->id)) {
                return;
            }
            const auto f = state.Owner(a_origin->id);
            if (!IsAiFaction(f) || cooling.contains(f) || perFaction[f] >= cfg.maxAttacksPerFaction) {
                return;
            }
            const auto o = state.Owner(a_target->id);
            if (o == f) {
                return;
            }
            if (o == kNeutral) {
                if (!cfg.expandIntoNeutral ||
                    std::find(cfg.expandTypes.begin(), cfg.expandTypes.end(), a_target->type) == cfg.expandTypes.end()) {
                    return;
                }
            } else if (!dipl.AtWar(f, o)) {
                peaceful.emplace_back(f, o);
                return;
            } else if (!cfg.attackPlayer && IsPlayerFaction(o)) {
                return;
            }
            if (!CanTarget(*a_target)) {
                return;
            }
            const auto defense = DefenseOf(*a_target, o);
            const auto need    = std::max(defense * cfg.minAdvantage * 1.25 - cfg.attackBase, 0.0) * cfg.goldPerStrength;
            const auto cost    = std::clamp(need, cfg.minGoldPerAttack, std::max(cfg.minGoldPerAttack, cfg.maxGoldPerAttack));
            const auto power   = cfg.attackBase + cost / cfg.goldPerStrength;
            if (cost > budgetOf(f) || power < defense * cfg.minAdvantage) {
                return;
            }
            auto& c = best[f];
            // Mais barato vence; empate -> prefere inimigo em guerra a neutro.
            if (!c.target || cost < c.gold || (cost == c.gold && o != kNeutral && c.defender == kNeutral)) {
                c = { a_origin, a_target, o, cost, power };
            }
        };

        const auto& cfgAll = Config::Get();
        const auto  limit  = std::min(all.size(), std::max<std::size_t>(cfg.maxEvaluationsPerTick, 1));
        for (std::size_t i = 0; i < limit; ++i) {
            const auto* t = all[(cursor + i) % all.size()];
            ++s.evaluated;
            if (t->type == TerritoryType::kClaim) {
                // Claims (M8) não aparecem nos vizinhos dos estáticos: o caminho é o inverso.
                for (const auto n : t->neighbors) {
                    consider(cfgAll.Find(n), t);
                }
            } else if (IsAiFaction(state.Owner(t->id))) {
                for (const auto n : t->neighbors) {
                    consider(t, cfgAll.Find(n));
                }
            }
        }

        // Lança os ataques escolhidos (teto global), pagando o ouro do saldo da IA.
        std::vector<Attack> launched;
        for (const auto& [f, c] : best) {
            if (active + launched.size() >= cfg.maxActiveAttacks) {
                break;
            }
            if (!econ.Spend(f, gold, c.gold)) {
                continue;
            }
            const auto dist  = c.origin->hasPosition && c.target->hasPosition ? Distance2D(c.origin->position, c.target->position) : 0.0f;
            const auto hours = std::clamp(static_cast<std::uint32_t>(std::lround(dist / 10000.0 * cfg.hoursPer10kUnits)),
                                          cfg.minTravelHours, cfg.maxTravelHours);
            Attack a;
            a.attacker   = f;
            a.origin     = c.origin->id;
            a.target     = c.target->id;
            a.defender   = c.defender;
            a.strength   = c.strength;
            a.hoursLeft  = hours;
            a.totalHours = hours;
            launched.push_back(a);
        }
        {
            std::scoped_lock l{ lock_ };
            for (auto& a : launched) {
                a.id = MakeId(std::format("attack/{}", serial_++));
                attacks_.push_back(a);
                cooldown_[a.attacker] = cfg.cooldownHours;
            }
            cursor_ = (cursor + limit) % all.size();
        }
        auto& factions = Factions::Get();
        for (const auto& a : launched) {
            const auto* t = cfgAll.Find(a.target);
            logger::info("IA: '{}' ataca '{}' (de '{}', dono '{}'): força {:.1f}, chega em {} h.", factions.NameOf(a.attacker),
                         t ? t->key : "?"s, cfgAll.NameOf(a.origin), factions.NameOf(a.defender), a.strength, a.hoursLeft);
            if (t && IsPlayerFaction(a.defender)) {
                I18n::NotifyTr("ai.attackLaunched", factions.NameOf(a.attacker), t->name, a.hoursLeft);
            }
        }
        s.launched = static_cast<std::uint32_t>(launched.size());

        // Agressão: facção de IA bem mais forte que um vizinho em paz pode declarar guerra (uma por facção).
        std::unordered_set<Id> declared;
        const auto             now = static_cast<std::uint64_t>(HoursNow());
        for (const auto& [f, o] : peaceful) {
            const auto it = aggression_.find(f);
            if (it == aggression_.end() || it->second <= 0.0 || declared.contains(f) ||
                dipl.RelationOf(f, o) != Diplomacy::Relation::kPeace) {
                continue;
            }
            if (Unit((static_cast<std::uint64_t>(f) << 32) ^ o ^ (now << 8)) >= it->second) {
                continue;
            }
            const auto pf = dipl.Power(f);
            const auto po = dipl.Power(o);
            if (pf >= po * cfg.declareWarRatio && dipl.DeclareWar(f, o) == Diplomacy::Result::kOk) {
                declared.insert(f);
                logger::info("IA: '{}' (poder {:.1f}) declarou guerra a '{}' (poder {:.1f}).", factions.NameOf(f), pf,
                             factions.NameOf(o), po);
            }
        }
        s.declared = static_cast<std::uint32_t>(declared.size());
        s.ms       = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        logger::info("[tempo] Ai::Strategic ({} território(s), {} ataque(s) novo(s)): {:.3f} ms", s.evaluated,
                     s.launched, s.ms);
        return s;
    }

    void Ai::DebugForceTick()
    {
        const auto s       = Strategic();
        const auto attacks = ActiveAttacks();
        I18n::NotifyTr("ai.debugTick", s.launched, s.declared, attacks.size(), std::format("{:.2f}", s.ms));
        if (attacks.empty()) {
            I18n::NotifyTr("ai.debugNone");
        }
        const auto& cfg      = Config::Get();
        auto&       factions = Factions::Get();
        std::size_t shown    = 0;
        for (const auto& a : attacks) {
            const auto* t = cfg.Find(a.target);
            logger::info("IA (debug): ataque 0x{:08X}: '{}' -> '{}' (de '{}'), força {:.1f}, faltam {} h{}.", a.id,
                         factions.NameOf(a.attacker), t ? t->key : "?"s, cfg.NameOf(a.origin), a.strength, a.hoursLeft,
                         a.engaged ? " (em batalha)" : "");
            if (shown++ < 5) {
                I18n::NotifyTr("ai.debugAttack", factions.NameOf(a.attacker), t ? t->name : "?"s,
                               static_cast<int>(a.strength), a.hoursLeft);
            }
        }
        const auto gold = Routes::Get().ResourceKey();
        for (const auto id : aiFactions_) {
            logger::info("IA (debug): '{}' ouro {:.1f}, poder {:.1f}.", factions.NameOf(id),
                         Economy::Get().Balance(id, gold), Diplomacy::Get().Power(id));
        }
    }

    void Ai::OnPostLoad()
    {
        const auto seat = Seat::Get().SeatTerritory();
        const auto mine = PlayerFactionId();
        const bool lost = seat != kNeutral && State::Get().Owner(seat) != mine;
        {
            std::scoped_lock l{ lock_ };
            depotSuspended_ = lost;
        }
        if (lost) {
            Economy::Get().SetDepot(nullptr);
            logger::info("IA: sede ocupada por '{}' no save; depósito suspenso.", Factions::Get().NameOf(State::Get().Owner(seat)));
        }
    }

    // ---- Cosave ----------------------------------------------------------------------------------------

    void Ai::Clear()
    {
        std::scoped_lock l{ lock_ };
        attacks_.clear();
        cooldown_.clear();
        sinceTick_      = 0;
        serial_         = 1;
        cursor_         = 0;
        depotSuspended_ = false;
    }

    // 'WARS' v1: u32 horas desde a última avaliação, u32 próximo serial,
    //   u32 nAtaques × { u32 id, u32 atacante, u32 origem, u32 alvo, u32 defensor, f64 força, u32 faltam, u32 total },
    //   u32 nEsperas × { u32 facção, u32 horas }.
    void Ai::Save(SKSE::SerializationInterface* a_intfc) const
    {
        std::scoped_lock l{ lock_ };
        if (!a_intfc->OpenRecord(kRecord, kRecordVersion)) {
            logger::error("Cosave: falha ao abrir o registro WARS.");
            return;
        }
        a_intfc->WriteRecordData(sinceTick_);
        a_intfc->WriteRecordData(serial_);
        a_intfc->WriteRecordData(static_cast<std::uint32_t>(attacks_.size()));
        for (const auto& a : attacks_) {
            a_intfc->WriteRecordData(a.id);
            a_intfc->WriteRecordData(a.attacker);
            a_intfc->WriteRecordData(a.origin);
            a_intfc->WriteRecordData(a.target);
            a_intfc->WriteRecordData(a.defender);
            a_intfc->WriteRecordData(a.strength);
            a_intfc->WriteRecordData(a.hoursLeft);
            a_intfc->WriteRecordData(a.totalHours);
        }
        a_intfc->WriteRecordData(static_cast<std::uint32_t>(cooldown_.size()));
        for (const auto& [f, left] : cooldown_) {
            a_intfc->WriteRecordData(f);
            a_intfc->WriteRecordData(left);
        }
    }

    void Ai::LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version)
    {
        if (a_version != kRecordVersion) {
            logger::warn("Cosave: versão {} do registro WARS não suportada; ataques descartados.", a_version);
            return;
        }
        std::scoped_lock l{ lock_ };
        std::uint32_t    count{};
        a_intfc->ReadRecordData(sinceTick_);
        a_intfc->ReadRecordData(serial_);
        a_intfc->ReadRecordData(count);
        std::size_t dropped = 0;
        for (std::uint32_t i = 0; i < count; ++i) {
            Attack a;
            a_intfc->ReadRecordData(a.id);
            a_intfc->ReadRecordData(a.attacker);
            a_intfc->ReadRecordData(a.origin);
            a_intfc->ReadRecordData(a.target);
            a_intfc->ReadRecordData(a.defender);
            a_intfc->ReadRecordData(a.strength);
            a_intfc->ReadRecordData(a.hoursLeft);
            a_intfc->ReadRecordData(a.totalHours);
            // Batalha em tempo real (M21) não sobrevive ao save: o ataque resolve no próximo tick (hoursLeft 0 -> 1).
            a.hoursLeft = std::max(a.hoursLeft, 1u);
            if (!Config::Get().Find(a.target) || !Factions::Get().Find(a.attacker)) {
                ++dropped;
                continue;
            }
            attacks_.push_back(a);
        }
        std::uint32_t waits{};
        a_intfc->ReadRecordData(waits);
        for (std::uint32_t i = 0; i < waits; ++i) {
            Id            f{};
            std::uint32_t left{};
            a_intfc->ReadRecordData(f);
            a_intfc->ReadRecordData(left);
            cooldown_[f] = left;
        }
        logger::info("Cosave: {} ataque(s) da IA carregado(s), {} descartado(s); próxima avaliação em {} h.",
                     attacks_.size(), dropped, sinceTick_);
    }
}
