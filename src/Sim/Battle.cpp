#include "Sim/Battle.h"

#include "Banner/BannerController.h"
#include "Core/Factions.h"
#include "Core/I18n.h"
#include "Core/State.h"
#include "Core/Timer.h"
#include "Sim/Army.h"
#include "Sim/Garrison.h"
#include "World/Conquest.h"
#include "World/Seat.h"

namespace AoS
{
    namespace
    {
        [[nodiscard]] float Distance2D(const RE::NiPoint3& a_a, const RE::NiPoint3& a_b)
        {
            const auto dx = a_a.x - a_b.x;
            const auto dy = a_a.y - a_b.y;
            return std::sqrt(dx * dx + dy * dy);
        }

        // Mesmo splitmix64 de Ai.cpp/Army.cpp: reproduzível no log, sem estado próprio.
        [[nodiscard]] double Unit(std::uint64_t a_seed)
        {
            std::uint64_t z = a_seed + 0x9E3779B97F4A7C15ull;
            z               = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z               = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            z ^= z >> 31;
            return static_cast<double>(z >> 11) * (1.0 / 9007199254740992.0);
        }

        [[nodiscard]] double HoursNow()
        {
            const auto cal = RE::Calendar::GetSingleton();
            return cal ? static_cast<double>(cal->GetHoursPassed()) : 0.0;
        }

        [[nodiscard]] bool CellAttached(const RE::TESObjectREFR* a_ref)
        {
            const auto cell = a_ref ? a_ref->GetParentCell() : nullptr;
            return cell && cell->IsAttached() && !a_ref->IsDeleted();
        }

        // Eventos que já acontecem (mesmo padrão de Garrison::WorldSink): célula carregada / jogador mudou de
        // local -> checagem de abandono coalescida; morte e descarga de célula só fazem trabalho se o ator é
        // nosso (refs criadas em runtime são 0xFF......).
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
                Battle::Get().RequestCheck();
                return RE::BSEventNotifyControl::kContinue;
            }

            RE::BSEventNotifyControl ProcessEvent(const RE::TESActorLocationChangeEvent* a_event,
                                                  RE::BSTEventSource<RE::TESActorLocationChangeEvent>*) override
            {
                if (a_event && a_event->actor && a_event->actor->IsPlayerRef()) {
                    Battle::Get().RequestCheck();
                }
                return RE::BSEventNotifyControl::kContinue;
            }

            RE::BSEventNotifyControl ProcessEvent(const RE::TESDeathEvent* a_event,
                                                  RE::BSTEventSource<RE::TESDeathEvent>*) override
            {
                if (a_event && a_event->dead && a_event->actorDying && a_event->actorDying->IsDynamicForm()) {
                    Battle::Get().OnActorDeath(a_event->actorDying->GetFormID());
                }
                return RE::BSEventNotifyControl::kContinue;
            }

            RE::BSEventNotifyControl ProcessEvent(const RE::TESCellAttachDetachEvent* a_event,
                                                  RE::BSTEventSource<RE::TESCellAttachDetachEvent>*) override
            {
                if (a_event && a_event->reference && a_event->reference->IsDynamicForm()) {
                    Battle::Get().OnActorAttach(a_event->reference->GetFormID(), a_event->attached);
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };
    }

    Battle& Battle::Get() noexcept
    {
        static Battle instance;
        return instance;
    }

    // ---- Carga / integração ---------------------------------------------------------------------------------

    void Battle::Load()
    {
        ScopedTimer timer{ "Battle::Load" };
        if (!factionA_ || !factionB_) {
            // Facções de combate PRÓPRIAS (ADR 0065): nunca as vanilla. Criadas uma vez em runtime (formulário
            // dinâmico, sem ESP) e mantidas hostis entre si pelo resto da sessão; reaproveitadas por TODAS as
            // batalhas (na prática só uma de cada vez fica perto do jogador).
            const auto factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::TESFaction>();
            factionA_ = factory ? factory->Create() : nullptr;
            factionB_ = factory ? factory->Create() : nullptr;
            if (factionA_ && factionB_) {
                factionA_->SetEnemy(factionB_, false, false);
                logger::info("Batalha: facções de combate próprias criadas (0x{:08X} / 0x{:08X}).",
                             factionA_->GetFormID(), factionB_->GetFormID());
            } else {
                logger::error("Batalha: IFormFactory não criou as facções de combate; M21 fica só numérica.");
            }
        }

        const auto& cfg = Config::Get().Battle();
        if (cfg.realTime && factionA_ && factionB_) {
            Ai::Get().SetNearPlayerHandler(
                [](const Ai::Attack& a_attack) { return Battle::Get().OnAttackArrivedNearPlayer(a_attack); });
            Army::Get().SetClashHandler([](Id a_a, Id a_b) { return Battle::Get().OnArmiesClashed(a_a, a_b); });
        } else {
            // Sem gancho registrado: Ai/Army caem automaticamente na resolução numérica já existente.
            Ai::Get().SetNearPlayerHandler(nullptr);
            Army::Get().SetClashHandler(nullptr);
        }

        static bool registered = false;
        if (!registered) {
            registered = true;
            if (const auto holder = RE::ScriptEventSourceHolder::GetSingleton()) {
                holder->AddEventSink<RE::TESCellFullyLoadedEvent>(WorldSink::Get());
                holder->AddEventSink<RE::TESActorLocationChangeEvent>(WorldSink::Get());
                holder->AddEventSink<RE::TESDeathEvent>(WorldSink::Get());
                holder->AddEventSink<RE::TESCellAttachDetachEvent>(WorldSink::Get());
            }
        }
        logger::info("Batalha: {}, teto {} ator(es) (próprio, não soma com a guarnição), {} por frame, raio {}.",
                     cfg.realTime ? "tempo real" : "só resolução numérica", cfg.maxActors, cfg.spawnPerFrame,
                     cfg.radius);
    }

    void Battle::SetCaptureHandler(CaptureHandler a_handler)
    {
        auto&            b = Get();
        std::scoped_lock l{ b.lock_ };
        b.capture_ = std::move(a_handler);
        logger::info("Batalha: gancho de captura (M25, opcional) {}.", b.capture_ ? "registrado" : "nenhum");
    }

    // ---- Gatilhos (M16/M20) -----------------------------------------------------------------------------------

    Battle::Side Battle::BuildSide(Id a_faction, std::uint32_t a_count, const Troops::Troop* a_troopHint) const
    {
        Side s;
        s.faction    = a_faction;
        s.troop      = a_troopHint ? a_troopHint : Troops::Get().TroopFor(a_faction);
        s.totalCount = s.troop ? a_count : 0;
        return s;
    }

    bool Battle::OnAttackArrivedNearPlayer(const Ai::Attack& a_attack)
    {
        const auto* t = Config::Get().Find(a_attack.target);
        if (!t) {
            return false;
        }
        const auto  defenderCount = Garrison::Get().Size(a_attack.target);
        const auto* defenderTroop = Troops::Get().TroopFor(a_attack.defender);
        const auto* attackerTroop = Troops::Get().TroopFor(a_attack.attacker);
        if (!defenderTroop || !attackerTroop || defenderCount == 0) {
            return false;  // sem tropa de campo para um dos lados, ou guarnição já vazia: resolução numérica normal
        }
        const auto attackerCount = std::max<std::uint32_t>(
            1, static_cast<std::uint32_t>(std::lround(a_attack.strength / std::max(0.01, attackerTroop->strength))));
        const auto sideA = BuildSide(a_attack.attacker, attackerCount, attackerTroop);
        const auto sideB = BuildSide(a_attack.defender, defenderCount, defenderTroop);
        const auto id    = StartInstance(a_attack.target, Source::kAiAttack, sideA, sideB, a_attack.id, kNeutral, kNeutral);
        if (id == kNeutral) {
            return false;
        }
        logger::info("Batalha: materializando ataque 0x{:08X} de '{}' a '{}' ({} x {} soldados).", a_attack.id,
                     Factions::Get().NameOf(a_attack.attacker), t->key, attackerCount, defenderCount);
        I18n::NotifyTr("battle.started", t->name, Factions::Get().NameOf(a_attack.attacker));
        return true;
    }

    bool Battle::OnArmiesClashed(Id a_armyA, Id a_armyB)
    {
        const auto ea = Army::Get().Find(a_armyA);
        const auto eb = Army::Get().Find(a_armyB);
        if (!ea || !eb) {
            return false;
        }
        const auto dominant = [](const Army::Entity& e) {
            Id     best{ kNeutral };
            double bestCount = -1.0;
            for (const auto& u : e.units) {
                if (u.count > bestCount) {
                    bestCount = u.count;
                    best      = u.troop;
                }
            }
            return best;
        };
        const auto* troopA = Troops::Get().Find(dominant(*ea));
        if (!troopA) {
            troopA = Troops::Get().TroopFor(ea->faction);
        }
        const auto* troopB = Troops::Get().Find(dominant(*eb));
        if (!troopB) {
            troopB = Troops::Get().TroopFor(eb->faction);
        }
        const auto countA = static_cast<std::uint32_t>(std::floor(ea->TotalCount()));
        const auto countB = static_cast<std::uint32_t>(std::floor(eb->TotalCount()));
        if (!troopA || !troopB || countA == 0 || countB == 0) {
            return false;
        }
        const auto sideA = BuildSide(ea->faction, countA, troopA);
        const auto sideB = BuildSide(eb->faction, countB, troopB);
        const auto id = StartInstance(ea->territory, Source::kArmyClash, sideA, sideB, kNeutral, a_armyA, a_armyB);
        if (id == kNeutral) {
            return false;
        }
        logger::info("Batalha: exércitos 0x{:08X} ('{}') x 0x{:08X} ('{}') se enfrentam em '{}'.", a_armyA,
                     Factions::Get().NameOf(ea->faction), a_armyB, Factions::Get().NameOf(eb->faction),
                     Config::Get().NameOf(ea->territory));
        I18n::NotifyTr("battle.armiesClash", Factions::Get().NameOf(ea->faction), Factions::Get().NameOf(eb->faction),
                       Config::Get().NameOf(ea->territory));
        return true;
    }

    // ---- Materialização (lotes por frame, como Garrison) ------------------------------------------------------

    RE::TESObjectREFR* Battle::FindAnchor(const Territory& a_territory)
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

    RE::NiPoint3 Battle::SlotPosition(const RE::TESObjectREFR& a_anchor, bool a_sideIsA, std::uint32_t a_index)
    {
        const auto& cfg     = Config::Get().Battle();
        auto        pos     = a_anchor.GetPosition();
        const auto  slot    = a_index % 3;
        const auto  row     = a_index / 3;
        const float lateral = slot == 1 ? cfg.formationSpacing : (slot == 2 ? -cfg.formationSpacing : 0.0f);
        const float sideDir = a_sideIsA ? 1.0f : -1.0f;
        pos.x += lateral;
        pos.y += sideDir * (cfg.sideDistance + static_cast<float>(row) * cfg.formationSpacing * 0.6f);
        pos.z += 32.0f;  // mesmo ajuste de queda que a Garrison usa ao nascer sobre o marcador
        return pos;
    }

    Id Battle::StartInstance(Id a_territory, Source a_source, Side a_sideA, Side a_sideB, Id a_attackId, Id a_armyA,
                             Id a_armyB)
    {
        if (!factionA_ || !factionB_ || a_sideA.totalCount == 0 || a_sideB.totalCount == 0) {
            return kNeutral;
        }
        const auto* t      = Config::Get().Find(a_territory);
        const auto  anchor = t ? FindAnchor(*t) : nullptr;
        if (!anchor) {
            logger::info("Batalha: sem marcador carregado em '{}'; resolução numérica.", t ? t->key : "?"s);
            return kNeutral;
        }
        Instance inst;
        inst.territory = a_territory;
        inst.anchor    = anchor->GetFormID();
        inst.source    = a_source;
        inst.attackId  = a_attackId;
        inst.armyA     = a_armyA;
        inst.armyB     = a_armyB;
        inst.a         = a_sideA;
        inst.b         = a_sideB;
        {
            std::scoped_lock l{ lock_ };
            inst.id = MakeId(std::format("battle/{}", serial_++));
            instances_.push_back(inst);
        }
        KickstartSpawns(inst.id);
        return inst.id;
    }

    void Battle::KickstartSpawns(Id a_instance)
    {
        std::uint32_t totalA{}, totalB{};
        {
            std::scoped_lock l{ lock_ };
            const auto it = std::find_if(instances_.begin(), instances_.end(), [&](const Instance& i) { return i.id == a_instance; });
            if (it == instances_.end()) {
                return;
            }
            totalA = it->a.totalCount;
            totalB = it->b.totalCount;
        }
        auto budget      = Config::Get().Battle().maxActors;
        budget           = budget > aliveTotal_.load() ? budget - aliveTotal_.load() : 0;
        const auto wantA = std::min(totalA, budget / 2);
        budget -= wantA;
        const auto wantB = std::min(totalB, budget);
        if (wantA > 0) {
            Enqueue(a_instance, true, wantA);
        }
        if (wantB > 0) {
            Enqueue(a_instance, false, wantB);
        }
    }

    void Battle::Enqueue(Id a_instance, bool a_sideIsA, std::uint32_t a_count)
    {
        if (a_count == 0) {
            return;
        }
        std::uint32_t startIndex = 0;
        {
            std::scoped_lock l{ lock_ };
            const auto it = std::find_if(instances_.begin(), instances_.end(), [&](const Instance& i) { return i.id == a_instance; });
            if (it == instances_.end()) {
                return;
            }
            auto& side = a_sideIsA ? it->a : it->b;
            startIndex = side.spawned;  // posição na formação segue o cumulativo já reservado
            side.spawned += a_count;    // reserva de antemão (evita duas Enqueue passarem do total abstrato)
            queue_.push_back({ a_instance, a_sideIsA, a_count, startIndex });
        }
        if (!processing_.exchange(true)) {
            SKSE::GetTaskInterface()->AddTask([] { Battle::Get().ProcessQueue(); });
        }
    }

    // Lote por frame: no máximo battle.spawnPerFrame atores, depois reagenda (AddTask), como Garrison::ProcessQueue.
    void Battle::ProcessQueue()
    {
        const auto start  = std::chrono::steady_clock::now();
        auto       budget = Config::Get().Battle().spawnPerFrame;
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
        const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (ms > Config::Get().Battle().maxFrameMs) {
            logger::warn("[tempo] Batalha::ProcessQueue passou de {:.1f} ms: {:.3f} ms.", Config::Get().Battle().maxFrameMs,
                         ms);
        } else {
            logger::info("[tempo] Batalha::ProcessQueue: {:.3f} ms.", ms);
        }
        bool more;
        {
            std::scoped_lock l{ lock_ };
            more = !queue_.empty();
        }
        if (more) {
            SKSE::GetTaskInterface()->AddTask([] { Battle::Get().ProcessQueue(); });
            return;
        }
        processing_ = false;
    }

    bool Battle::SpawnOne(Job& a_job)
    {
        RE::FormID           anchorId{ 0 };
        const Troops::Troop* troop = nullptr;
        {
            std::scoped_lock l{ lock_ };
            const auto it = std::find_if(instances_.begin(), instances_.end(), [&](const Instance& i) { return i.id == a_job.instance; });
            if (it == instances_.end()) {
                return false;  // instância já concluída
            }
            anchorId = it->anchor;
            troop    = a_job.sideIsA ? it->a.troop : it->b.troop;
        }
        const auto anchor = RE::TESForm::LookupByID<RE::TESObjectREFR>(anchorId);
        if (!anchor || !CellAttached(anchor)) {
            logger::info("Batalha: âncora não está mais carregada; lote cancelado.");
            return false;
        }
        if (!troop || !troop->actor) {
            return false;
        }
        if (aliveTotal_.load() >= Config::Get().Battle().maxActors) {
            logger::info("Batalha: teto global de {} ator(es) atingido.", Config::Get().Battle().maxActors);
            return false;
        }
        const auto ref   = anchor->PlaceObjectAtMe(troop->actor, /*forcePersist*/ false);
        const auto actor = ref ? ref->As<RE::Actor>() : nullptr;
        if (!actor) {
            logger::error("Batalha: PlaceObjectAtMe falhou para 0x{:08X}.", troop->actor->GetFormID());
            if (ref) {
                ref->Disable();
                ref->SetDelete(true);
            }
            return false;
        }
        actor->SetPosition(SlotPosition(*anchor, a_job.sideIsA, a_job.index), /*updateCharController*/ true);
        const auto combat = a_job.sideIsA ? factionA_ : factionB_;
        if (combat) {
            actor->AddToFaction(combat, 0);  // facção de combate PRÓPRIA (nunca vanilla, ADR 0065)
        }
        Troops::Get().EnsureOutfit(actor, *troop);
        actor->EvaluatePackage();
        const auto id       = actor->GetFormID();
        const auto troopKey = MakeId(troop->key);
        {
            std::scoped_lock l{ lock_ };
            const auto it = std::find_if(instances_.begin(), instances_.end(), [&](const Instance& i) { return i.id == a_job.instance; });
            if (it == instances_.end()) {
                ref->Disable();
                ref->SetDelete(true);
                return false;
            }
            auto& side = a_job.sideIsA ? it->a : it->b;
            ++side.alive;
            live_[id] = { a_job.instance, a_job.sideIsA };
        }
        ++aliveTotal_;
        anyLive_ = true;
        // "Tropas peladas" (ADR 0044): o 3D pode ainda não existir; confere o outfit de novo no próximo frame.
        SKSE::GetTaskInterface()->AddTask([id, troopKey] {
            const auto a = RE::TESForm::LookupByID<RE::Actor>(id);
            const auto t = Troops::Get().Find(troopKey);
            if (a && t) {
                Troops::Get().EnsureOutfit(a, *t);
            }
        });
        logger::info("Batalha: soldado 0x{:08X} ('{}', lado {}) criado ({} vivo(s) no total).", id, actor->GetName(),
                     a_job.sideIsA ? 'A' : 'B', aliveTotal_.load());
        return true;
    }

    void Battle::Despawn(RE::FormID a_actor)
    {
        Id   instanceId{ kNeutral };
        bool sideIsA = false;
        bool found   = false;
        {
            std::scoped_lock l{ lock_ };
            const auto it = live_.find(a_actor);
            if (it == live_.end()) {
                return;
            }
            instanceId = it->second.first;
            sideIsA    = it->second.second;
            live_.erase(it);
            if (aliveTotal_ > 0) {
                --aliveTotal_;
            }
            anyLive_ = !live_.empty();
            found    = true;
        }
        if (found) {
            std::scoped_lock l{ lock_ };
            const auto it = std::find_if(instances_.begin(), instances_.end(), [&](const Instance& i) { return i.id == instanceId; });
            if (it != instances_.end()) {
                auto& side = sideIsA ? it->a : it->b;
                if (side.alive > 0) {
                    --side.alive;
                }
            }
        }
        if (const auto ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_actor)) {
            ref->Disable();
            ref->SetDelete(true);
        }
        logger::info("Batalha: soldado 0x{:08X} removido do mundo (célula descarregada).", a_actor);
    }

    // ---- Sinks ---------------------------------------------------------------------------------------------

    void Battle::OnActorDeath(RE::FormID a_actor)
    {
        if (!anyLive_.load()) {
            return;
        }
        Id   instanceId{ kNeutral };
        bool sideIsA = false;
        {
            std::scoped_lock l{ lock_ };
            const auto it = live_.find(a_actor);
            if (it == live_.end()) {
                return;
            }
            instanceId = it->second.first;
            sideIsA    = it->second.second;
            live_.erase(it);
            if (aliveTotal_ > 0) {
                --aliveTotal_;
            }
            anyLive_ = !live_.empty();
        }
        bool conclude  = false;
        bool reinforce = false;
        {
            std::scoped_lock l{ lock_ };
            const auto it = std::find_if(instances_.begin(), instances_.end(), [&](const Instance& i) { return i.id == instanceId; });
            if (it == instances_.end()) {
                return;
            }
            auto& side = sideIsA ? it->a : it->b;
            if (side.alive > 0) {
                --side.alive;
            }
            ++side.casualties;
            if (side.casualties >= side.totalCount) {
                conclude = true;
            } else if (side.spawned < side.totalCount) {
                reinforce = true;
            }
        }
        if (conclude) {
            FinishInstance(instanceId, /*abandoned*/ false);
        } else if (reinforce) {
            Enqueue(instanceId, sideIsA, 1);  // reforço: repõe 1 soldado do lado que perdeu o ator
        }
    }

    void Battle::OnActorAttach(RE::FormID a_actor, bool a_attached)
    {
        if (a_attached || !anyLive_.load()) {
            return;
        }
        // Célula descarregou: o ator de batalha volta a ser só número (a checagem de abandono, disparada pelo
        // mesmo evento de local, conclui a batalha em seguida se o jogador realmente saiu da área).
        SKSE::GetTaskInterface()->AddTask([a_actor] { Battle::Get().Despawn(a_actor); });
    }

    void Battle::RequestCheck()
    {
        if (checkQueued_.exchange(true)) {
            return;
        }
        SKSE::GetTaskInterface()->AddTask([] {
            Battle::Get().checkQueued_ = false;
            Battle::Get().CheckAbandon();
        });
    }

    void Battle::CheckAbandon()
    {
        const auto player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->GetParentCell()) {
            return;
        }
        const auto& cfg  = Config::Get().Battle();
        const auto  ppos = player->GetPosition();
        std::vector<Id> toFinish;
        {
            std::scoped_lock l{ lock_ };
            for (const auto& inst : instances_) {
                const auto anchor   = RE::TESForm::LookupByID<RE::TESObjectREFR>(inst.anchor);
                const bool stillNear = anchor && CellAttached(anchor) && Distance2D(anchor->GetPosition(), ppos) <= cfg.radius;
                if (!stillNear) {
                    toFinish.push_back(inst.id);
                }
            }
        }
        for (const auto id : toFinish) {
            FinishInstance(id, /*abandoned*/ true);
        }
    }

    // ---- Conclusão: despawna e aplica o resultado (Ai/Army/Garrison) ------------------------------------------

    void Battle::FinishInstance(Id a_instance, bool a_abandoned)
    {
        Instance                copy;
        std::vector<RE::FormID> actors;
        {
            std::scoped_lock l{ lock_ };
            const auto it = std::find_if(instances_.begin(), instances_.end(), [&](const Instance& i) { return i.id == a_instance; });
            if (it == instances_.end()) {
                return;  // já concluída (dois gatilhos quase simultâneos)
            }
            copy = *it;
            instances_.erase(it);
            std::erase_if(queue_, [&](const Job& j) { return j.instance == a_instance; });
            for (const auto& [actorId, info] : live_) {
                if (info.first == a_instance) {
                    actors.push_back(actorId);
                }
            }
            for (const auto id : actors) {
                live_.erase(id);
            }
            anyLive_ = !live_.empty();
        }
        for (const auto id : actors) {
            if (aliveTotal_ > 0) {
                --aliveTotal_;
            }
            if (const auto ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(id)) {
                ref->Disable();
                ref->SetDelete(true);
            }
        }

        bool aWon;
        if (!a_abandoned) {
            aWon = copy.b.casualties >= copy.b.totalCount;  // lado B esgotado = lado A venceu
        } else {
            // Jogador saiu da área antes do fim: compara a força abstrata restante de cada lado (± battle.randomness,
            // mesma ideia do sorteio da IA, M16) em vez de inventar um empate.
            const auto remA = (copy.a.totalCount > copy.a.casualties ? copy.a.totalCount - copy.a.casualties : 0u) *
                              (copy.a.troop ? copy.a.troop->strength : 1.0);
            const auto remB = (copy.b.totalCount > copy.b.casualties ? copy.b.totalCount - copy.b.casualties : 0u) *
                              (copy.b.troop ? copy.b.troop->strength : 1.0);
            const auto& cfg  = Config::Get().Battle();
            const auto  now  = static_cast<std::uint64_t>(HoursNow());
            const auto  rollA = remA * (1.0 + cfg.randomness * (2.0 * Unit(a_instance ^ (now << 1)) - 1.0));
            const auto  rollB = remB * (1.0 + cfg.randomness * (2.0 * Unit(a_instance ^ (now << 1) ^ 1) - 1.0));
            aWon              = rollA >= rollB;
        }
        logger::info("Batalha: 0x{:08X} concluída ({}): '{}' ({} baixa(s)) x '{}' ({} baixa(s)); vence '{}'.",
                     a_instance, a_abandoned ? "abandonada" : "esgotada", Factions::Get().NameOf(copy.a.faction),
                     copy.a.casualties, Factions::Get().NameOf(copy.b.faction), copy.b.casualties,
                     Factions::Get().NameOf(aWon ? copy.a.faction : copy.b.faction));

        const auto losingFaction    = aWon ? copy.b.faction : copy.a.faction;
        const auto losingCasualties = aWon ? copy.b.casualties : copy.a.casualties;

        switch (copy.source) {
        case Source::kAiAttack:
            if (copy.b.casualties > 0) {
                // Baixas reais na guarnição defensora, mesmo se ela repeliu o ataque (Garrison::Add já existe,
                // M14); se o ataque venceu, Ai::Resolve reseta a guarnição para a inicial do novo dono de qualquer
                // forma (Garrison::OnOwnerChanged), então isto só importa quando o defensor repele.
                Garrison::Get().Add(copy.territory, -static_cast<std::int32_t>(copy.b.casualties));
            }
            Ai::Get().ResolveAttack(copy.attackId, /*attackerWon*/ aWon);
            break;
        case Source::kArmyClash: {
            const auto winnerArmy        = aWon ? copy.armyA : copy.armyB;
            const auto loserArmy         = aWon ? copy.armyB : copy.armyA;
            const auto winnerCasualties = aWon ? copy.a.casualties : copy.b.casualties;
            Army::Get().ApplyLosses(winnerArmy, winnerCasualties);
            Army::Get().Disband(loserArmy);
            break;
        }
        case Source::kDebug:
            I18n::NotifyTr("battle.debugResult", Factions::Get().NameOf(aWon ? copy.a.faction : copy.b.faction));
            break;
        }

        CaptureHandler handler;
        {
            std::scoped_lock l{ lock_ };
            handler = capture_;
        }
        if (handler && copy.source != Source::kDebug && losingCasualties > 0) {
            handler(losingFaction, copy.territory, losingCasualties);
        }
    }

    // ---- Debug ----------------------------------------------------------------------------------------------

    void Battle::DebugTestBattle()
    {
        const Territory* territory = Conquest::Get().TerritoryAtPlayer(/*onlyCleared*/ false);
        if (!territory) {
            if (const auto seat = Seat::Get().SeatTerritory(); seat != kNeutral) {
                territory = Config::Get().Find(seat);
            }
        }
        if (!territory) {
            I18n::NotifyTr("battle.noTerritory");
            return;
        }
        const auto player = Factions::Get().PlayerFaction();
        if (!player) {
            I18n::NotifyTr("army.noFaction");
            return;
        }
        Id enemy{ kNeutral };
        for (const auto& f : Factions::Get().All()) {
            if (!f.player) {
                enemy = f.id;
                break;
            }
        }
        if (enemy == kNeutral) {
            I18n::NotifyTr("battle.noEnemyFaction");
            return;
        }
        const auto n     = Config::Get().Debug().battleTestSize;
        const auto sideA = BuildSide(player->id, n, nullptr);
        const auto sideB = BuildSide(enemy, n, nullptr);
        if (!sideA.troop || !sideB.troop) {
            I18n::NotifyTr("battle.noTroop");
            return;
        }
        const auto id = StartInstance(territory->id, Source::kDebug, sideA, sideB, kNeutral, kNeutral, kNeutral);
        if (id == kNeutral) {
            I18n::NotifyTr("battle.noAnchor");
            return;
        }
        I18n::NotifyTr("battle.debugStarted", territory->name, Factions::Get().NameOf(enemy));
    }

    void Battle::Clear()
    {
        std::vector<RE::FormID> actors;
        {
            std::scoped_lock l{ lock_ };
            for (const auto& [id, info] : live_) {
                actors.push_back(id);
            }
            instances_.clear();
            live_.clear();
            queue_.clear();
            stale_.clear();
            aliveTotal_ = 0;
            anyLive_    = false;
        }
        for (const auto id : actors) {
            if (const auto ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(id)) {
                ref->Disable();
                ref->SetDelete(true);
            }
        }
    }

    // ---- Cosave ---------------------------------------------------------------------------------------------
    // 'BATL' v1: só a lista de atores vivos no momento do save (batalhas não sobrevivem ao save, ver o
    // comentário da classe); serve apenas para o OnPostLoad apagar soldados temporários órfãos.

    void Battle::Save(SKSE::SerializationInterface* a_intfc) const
    {
        std::scoped_lock l{ lock_ };
        if (live_.empty()) {
            return;
        }
        if (!a_intfc->OpenRecord(kRecord, kRecordVersion)) {
            logger::error("Cosave: falha ao abrir o registro BATL.");
            return;
        }
        a_intfc->WriteRecordData(static_cast<std::uint32_t>(live_.size()));
        for (const auto& [id, info] : live_) {
            a_intfc->WriteRecordData(id);
        }
    }

    void Battle::LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version)
    {
        if (a_version != kRecordVersion) {
            logger::warn("Cosave: versão {} do registro BATL não suportada; ignorado.", a_version);
            return;
        }
        std::scoped_lock l{ lock_ };
        std::uint32_t    count{};
        a_intfc->ReadRecordData(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            RE::FormID oldId{}, newId{};
            a_intfc->ReadRecordData(oldId);
            if (a_intfc->ResolveFormID(oldId, newId)) {
                stale_.push_back(newId);
            }
        }
        logger::info("Cosave: {} ator(es) de batalha do save anterior a remover.", stale_.size());
    }

    void Battle::OnPostLoad()
    {
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
            logger::info("Batalha: {} ator(es) de uma batalha interrompida pelo save removido(s).", removed);
        }
    }
}
