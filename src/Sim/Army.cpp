#include "Sim/Army.h"

#include "Core/Factions.h"
#include "Core/I18n.h"
#include "Core/State.h"
#include "Core/Timer.h"
#include "Realm/Modifiers.h"
#include "Realm/Population.h"
#include "Sim/Diplomacy.h"
#include "Sim/Economy.h"
#include "Sim/Routes.h"
#include "Sim/Troops.h"
#include "Stock/Stock.h"
#include "World/Conquest.h"
#include "World/Seat.h"

namespace AoS
{
    namespace
    {
        [[nodiscard]] bool IsPlayerFaction(Id a_faction)
        {
            const auto f = Factions::Get().Find(a_faction);
            return f && f->player;
        }

        [[nodiscard]] float Distance2D(const RE::NiPoint3& a_a, const RE::NiPoint3& a_b)
        {
            const auto dx = a_a.x - a_b.x;
            const auto dy = a_a.y - a_b.y;
            return std::sqrt(dx * dx + dy * dy);
        }

        // Número pseudoaleatório em [0, 1) a partir de uma semente (splitmix64), igual ao usado pela IA (M16):
        // reproduzível no log, sem estado próprio. Nome diferente de Army::Unit (struct aninhada) de propósito:
        // dentro de métodos da classe Army, um "Unit" sem qualificação resolveria para a struct, não esta função.
        [[nodiscard]] double RandomUnit(std::uint64_t a_seed)
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
    }

    double Army::Entity::TotalCount() const noexcept
    {
        double total = 0.0;
        for (const auto& u : units) {
            total += u.count;
        }
        return total;
    }

    double Army::Entity::TotalStrength() const
    {
        double total = 0.0;
        for (const auto& u : units) {
            const auto* troop = Troops::Get().Find(u.troop);
            total += u.count * (troop ? troop->strength : 1.0);
        }
        return total * (1.0 + static_cast<double>(officers) * Config::Get().Army().officerBonusEach);
    }

    Army& Army::Get() noexcept
    {
        static Army instance;
        return instance;
    }

    void Army::Load()
    {
        ScopedTimer timer{ "Army::Load" };
        const auto& cfg = Config::Get().Army();
        logger::info("Exército: {}, {} exército(s) carregado(s), teto de escolta {}, {} oficial(is) max.",
                     cfg.enabled ? "ligado" : "desligado", armies_.size(), cfg.maxEscort, cfg.maxOfficers);

        auto& eco = Economy::Get();
        eco.AddTickListener("army", [](std::uint32_t a_hours) { Army::Get().OnHours(a_hours); });
        // Manutenção (como Garrison::Load): chamada DENTRO do tick da economia (lock dela travado).
        eco.RegisterUpkeep("army", [this](std::vector<Economy::Upkeep>& a_out) {
            std::unordered_map<Id, bool> player;
            for (const auto& f : Factions::Get().All()) {
                player[f.id] = f.player;
            }
            const auto&                    aCfg = Config::Get().Army();
            std::unordered_map<Id, double> total;  // (facção, recurso) agregado via chave composta abaixo
            std::scoped_lock               l{ lock_ };
            for (const auto& a : armies_) {
                if (!player[a.faction] && !aCfg.aiUpkeep) {
                    continue;
                }
                for (const auto& u : a.units) {
                    const auto* troop = Troops::Get().Find(u.troop);
                    if (!troop) {
                        continue;
                    }
                    for (const auto& [resource, perHour] : troop->upkeep) {
                        total[(static_cast<std::uint64_t>(a.faction) << 32) ^ resource] += perHour * u.count;
                    }
                }
            }
            for (const auto& [key, amount] : total) {
                if (amount <= 0.0) {
                    continue;
                }
                const auto faction  = static_cast<Id>(key >> 32);
                const auto resource = static_cast<Id>(key & 0xFFFFFFFFu);
                a_out.push_back({ faction, resource, amount });
            }
        });
    }

    // ---- Recrutamento -------------------------------------------------------------------------------------

    Army::RecruitResult Army::Recruit(Id a_faction, Id a_territory, Id a_troop, std::uint32_t a_count)
    {
        if (!Config::Get().Army().enabled || a_count == 0) {
            return RecruitResult::kDisabled;
        }
        const auto* t = Config::Get().Find(a_territory);
        if (!t || State::Get().Owner(a_territory) != a_faction) {
            return RecruitResult::kWrongOwner;
        }
        const auto* found = Troops::Get().Find(a_troop);
        const auto* troop = found ? found : Troops::Get().TroopFor(a_faction);
        if (!troop) {
            return RecruitResult::kNoTroop;
        }
        const auto troopId = found ? a_troop : MakeId(troop->key);  // chave real (pode ter vindo do fallback)

        const auto  gold = Routes::Get().ResourceKey();
        const auto  mult = static_cast<double>(Realm::Modifiers::GetFor(a_faction, a_territory, Realm::Modifier::RecruitCost));
        const auto  maxCost = troop->goldCost * a_count * mult;
        if (Economy::Get().Balance(a_faction, gold) < maxCost) {
            return RecruitResult::kNoGold;
        }

        // Equipamento: só para facções com sede (ADR 0064: a IA não tem estoque real de itens; manutenção
        // continua valendo pelo gancho de upkeep acima). Sem categoria/sede, o passo é pulado.
        RE::TESObjectREFR* chest = nullptr;
        std::optional<std::size_t> categoryIdx;
        if (!troop->equipCategory.empty() && troop->equipAmount > 0 && IsPlayerFaction(a_faction)) {
            chest = Seat::Get().HasSeat() ? Seat::Get().Chest() : nullptr;
            if (chest) {
                categoryIdx = Stock::Get().CategoryIndex(troop->equipCategory);
                if (categoryIdx) {
                    const auto totals = Stock::Get().CountByCategory(chest);
                    const auto needed = troop->equipAmount * static_cast<std::int64_t>(a_count);
                    if (totals.perCategory[*categoryIdx] < needed) {
                        return RecruitResult::kNoEquipment;
                    }
                } else {
                    logger::warn("Exército: categoria de equipamento '{}' (tropa '{}') não existe em Resources.json; "
                                 "custo de equipamento ignorado.",
                                 troop->equipCategory, troop->key);
                }
            }
        }

        const auto drafted = Realm::Population::Get().Draft(a_territory, a_count);
        if (drafted == 0) {
            return RecruitResult::kNoPopulation;
        }

        const auto actualCost = troop->goldCost * drafted * mult;
        if (!Economy::Get().Spend(a_faction, gold, actualCost)) {
            // Não deveria acontecer (já verificamos o saldo para um valor >= este); loga e segue sem cobrar.
            logger::error("Exército: Spend falhou para '{}' depois de verificar o saldo; recruta sem custo de ouro.",
                          Factions::Get().NameOf(a_faction));
        }
        if (chest && categoryIdx) {
            const auto needed = troop->equipAmount * static_cast<std::int64_t>(drafted);
            Stock::Get().Take(chest, *categoryIdx, needed);
        }

        {
            std::scoped_lock l{ lock_ };
            training_.push_back({ a_faction, a_territory, troopId, drafted, troop->trainHours });
        }
        logger::info("Exército: '{}' recrutou {} '{}' em '{}' (treino em {} h).", Factions::Get().NameOf(a_faction),
                     drafted, troop->key, t->key, troop->trainHours);
        if (IsPlayerFaction(a_faction)) {
            I18n::NotifyTr("army.recruiting", static_cast<std::int64_t>(drafted), troop->key, troop->trainHours);
        }
        return RecruitResult::kOk;
    }

    // ---- Movimento e oficiais -----------------------------------------------------------------------------

    bool Army::Move(Id a_army, Id a_destination)
    {
        const auto* dest = Config::Get().Find(a_destination);
        if (!dest) {
            return false;
        }
        std::scoped_lock l{ lock_ };
        auto*            a = FindUnlocked(a_army);
        if (!a || a->destination != kNeutral) {
            return false;  // já em marcha
        }
        const auto* origin = Config::Get().Find(a->territory);
        if (!origin || std::find(origin->neighbors.begin(), origin->neighbors.end(), a_destination) ==
                           origin->neighbors.end()) {
            return false;  // só vizinhos (como a marcha da IA, M16)
        }
        const auto& cfg  = Config::Get().Army();
        const auto  dist = origin->hasPosition && dest->hasPosition ? Distance2D(origin->position, dest->position)
                                                                     : 0.0f;
        const auto  hours =
            std::clamp(static_cast<std::uint32_t>(std::lround(dist / 10000.0 * cfg.hoursPer10kUnits)),
                       cfg.minTravelHours, cfg.maxTravelHours);
        a->destination = a_destination;
        a->hoursLeft    = hours;
        a->totalHours   = hours;
        logger::info("Exército: 0x{:08X} ('{}') marcha de '{}' a '{}' ({} h).", a_army,
                     Factions::Get().NameOf(a->faction), origin->key, dest->key, hours);
        return true;
    }

    void Army::SetOfficers(Id a_army, std::uint32_t a_officers)
    {
        std::scoped_lock l{ lock_ };
        if (auto* a = FindUnlocked(a_army)) {
            a->officers = std::min(a_officers, Config::Get().Army().maxOfficers);
        }
    }

    bool Army::SetEscort(Id a_army, bool a_escort)
    {
        std::scoped_lock l{ lock_ };
        auto*            a = FindUnlocked(a_army);
        if (!a || a->escort == a_escort) {
            return a != nullptr;
        }
        if (a_escort) {
            double escorting = 0.0;
            for (const auto& o : armies_) {
                if (o.faction == a->faction && o.escort) {
                    escorting += o.TotalCount();
                }
            }
            if (escorting + a->TotalCount() > static_cast<double>(Config::Get().Army().maxEscort)) {
                return false;  // teto global de escolta cheio
            }
        }
        a->escort = a_escort;
        return true;
    }

    // ---- Consulta -------------------------------------------------------------------------------------------

    std::optional<Army::Entity> Army::Find(Id a_army) const
    {
        std::scoped_lock l{ lock_ };
        const auto*       a = FindUnlocked(a_army);
        return a ? std::optional{ *a } : std::nullopt;
    }

    std::vector<Army::Entity> Army::ArmiesOf(Id a_faction) const
    {
        std::scoped_lock          l{ lock_ };
        std::vector<Entity>       out;
        for (const auto& a : armies_) {
            if (a.faction == a_faction) {
                out.push_back(a);
            }
        }
        return out;
    }

    std::vector<Army::Entity> Army::At(Id a_territory) const
    {
        std::scoped_lock    l{ lock_ };
        std::vector<Entity> out;
        for (const auto& a : armies_) {
            if (a.territory == a_territory && a.destination == kNeutral) {
                out.push_back(a);
            }
        }
        return out;
    }

    std::uint32_t Army::EscortCount(Id a_faction) const
    {
        std::scoped_lock l{ lock_ };
        double           total = 0.0;
        for (const auto& a : armies_) {
            if (a.faction == a_faction && a.escort) {
                total += a.TotalCount();
            }
        }
        return static_cast<std::uint32_t>(std::floor(total));
    }

    // ---- M21: integração -------------------------------------------------------------------------------------

    void Army::SetClashHandler(ClashHandler a_handler)
    {
        std::scoped_lock l{ lock_ };
        clash_ = std::move(a_handler);
        logger::info("Exército: gancho de confronto (M21) {}.", clash_ ? "registrado" : "padrão (numérico)");
    }

    void Army::ApplyLosses(Id a_army, std::uint32_t a_losses)
    {
        std::scoped_lock l{ lock_ };
        auto*            a = FindUnlocked(a_army);
        if (!a || a_losses == 0) {
            return;
        }
        auto total = a->TotalCount();
        if (total <= 0.0) {
            return;
        }
        auto remaining = static_cast<double>(a_losses);
        for (auto& u : a->units) {
            if (remaining <= 0.0) {
                break;
            }
            const auto share = u.count / total * static_cast<double>(a_losses);
            const auto take   = std::min(u.count, share);
            u.count -= take;
            remaining -= take;
        }
        if (a->TotalCount() <= 0.01) {
            logger::info("Exército: 0x{:08X} ('{}') destruído em combate.", a_army, Factions::Get().NameOf(a->faction));
            std::erase_if(armies_, [&](const Entity& e) { return e.id == a_army; });
        }
    }

    void Army::Disband(Id a_army)
    {
        std::scoped_lock l{ lock_ };
        std::erase_if(armies_, [&](const Entity& e) { return e.id == a_army; });
    }

    // ---- Tick ------------------------------------------------------------------------------------------------

    void Army::OnHours(std::uint32_t a_hours)
    {
        if (!Config::Get().Army().enabled || a_hours == 0) {
            return;
        }
        ScopedTimer timer{ "Army::OnHours" };
        const auto& cfg = Config::Get().Army();

        std::unordered_map<Id, bool>          player;
        std::unordered_map<Id, std::uint32_t> deficit;
        for (const auto& f : Factions::Get().All()) {
            player[f.id]  = f.player;
            deficit[f.id] = Economy::Get().DeficitHours(f.id);
        }

        std::vector<Training> ready;
        {
            std::scoped_lock l{ lock_ };
            // Treino.
            for (auto it = training_.begin(); it != training_.end();) {
                it->hoursLeft = it->hoursLeft > a_hours ? it->hoursLeft - a_hours : 0;
                if (it->hoursLeft == 0) {
                    ready.push_back(*it);
                    it = training_.erase(it);
                } else {
                    ++it;
                }
            }
            // Marcha.
            for (auto& a : armies_) {
                if (a.destination == kNeutral) {
                    continue;
                }
                a.hoursLeft = a.hoursLeft > a_hours ? a.hoursLeft - a_hours : 0;
                if (a.hoursLeft == 0) {
                    logger::info("Exército: 0x{:08X} ('{}') chegou a '{}'.", a.id, Factions::Get().NameOf(a.faction),
                                 Config::Get().NameOf(a.destination));
                    a.territory    = a.destination;
                    a.destination  = kNeutral;
                }
            }
            // Deserção: facção em déficit de manutenção há mais de deficitGraceHours horas (como a guarnição, M14).
            for (auto& a : armies_) {
                if ((!player[a.faction] && !cfg.aiUpkeep) || deficit[a.faction] <= cfg.deficitGraceHours) {
                    continue;
                }
                const auto loss = cfg.desertPerHour * a_hours;
                auto       total = a.TotalCount();
                if (total <= 0.0) {
                    continue;
                }
                for (auto& u : a.units) {
                    u.count = std::max(0.0, u.count - loss * (u.count / total));
                }
            }
            std::erase_if(armies_, [](const Entity& e) { return e.TotalCount() <= 0.01; });
        }
        for (auto& r : ready) {
            std::scoped_lock l{ lock_ };
            auto&            a = StationedArmyOf(r.faction, r.territory);
            const auto       it = std::find_if(a.units.begin(), a.units.end(), [&](const Unit& u) { return u.troop == r.troop; });
            if (it != a.units.end()) {
                it->count += r.count;
            } else {
                a.units.push_back({ r.troop, static_cast<double>(r.count) });
            }
            const auto* troop = Troops::Get().Find(r.troop);
            logger::info("Exército: {} recruta(s) ('{}') entraram no exército 0x{:08X} de '{}' em '{}'.", r.count,
                         troop ? troop->key : "?"s, a.id, Factions::Get().NameOf(r.faction),
                         Config::Get().NameOf(r.territory));
        }
        CheckClashes();
    }

    void Army::CheckClashes()
    {
        // Agrupa exércitos parados por território; um confronto por território por tick é suficiente (lição
        // de desempenho: nada de laços caros aqui, só o necessário).
        std::unordered_map<Id, std::vector<Id>> byTerritory;
        {
            std::scoped_lock l{ lock_ };
            for (const auto& a : armies_) {
                if (a.destination == kNeutral) {
                    byTerritory[a.territory].push_back(a.id);
                }
            }
        }
        for (const auto& [territory, ids] : byTerritory) {
            if (ids.size() < 2) {
                continue;
            }
            for (std::size_t i = 0; i < ids.size(); ++i) {
                for (std::size_t j = i + 1; j < ids.size(); ++j) {
                    Id factionA{ kNeutral }, factionB{ kNeutral };
                    {
                        std::scoped_lock l{ lock_ };
                        const auto*      a = FindUnlocked(ids[i]);
                        const auto*      b = FindUnlocked(ids[j]);
                        if (!a || !b) {
                            continue;
                        }
                        factionA = a->faction;
                        factionB = b->faction;
                    }
                    if (factionA == factionB || !Diplomacy::Get().AtWar(factionA, factionB)) {
                        continue;
                    }
                    ClashHandler handler;
                    {
                        std::scoped_lock l{ lock_ };
                        handler = clash_;
                    }
                    if (handler && handler(ids[i], ids[j])) {
                        return;  // M21 assumiu; só um confronto materializado por vez
                    }
                    ResolveNumerically(ids[i], ids[j]);
                    return;  // reavalia o resto no próximo tick (evita encadear vários confrontos no mesmo lote)
                }
            }
        }
    }

    void Army::ResolveNumerically(Id a_armyA, Id a_armyB)
    {
        double strengthA{}, strengthB{};
        {
            std::scoped_lock l{ lock_ };
            const auto*      a = FindUnlocked(a_armyA);
            const auto*      b = FindUnlocked(a_armyB);
            if (!a || !b) {
                return;
            }
            strengthA = a->TotalStrength();
            strengthB = b->TotalStrength();
        }
        const auto& cfg  = Config::Get().Battle();
        const auto  now  = static_cast<std::uint64_t>(HoursNow());
        const auto  rollA = strengthA * (1.0 + cfg.randomness * (2.0 * RandomUnit(a_armyA ^ (now << 1)) - 1.0));
        const auto  rollB = strengthB * (1.0 + cfg.randomness * (2.0 * RandomUnit(a_armyB ^ (now << 1) ^ 1) - 1.0));
        const auto  loserId  = rollA >= rollB ? a_armyB : a_armyA;
        const auto  winnerId = rollA >= rollB ? a_armyA : a_armyB;
        const auto  loserStrength  = rollA >= rollB ? rollB : rollA;
        const auto  winnerStrength = rollA >= rollB ? rollA : rollB;
        logger::info("Exército: confronto numérico 0x{:08X} ({:.1f}) x 0x{:08X} ({:.1f}): vence 0x{:08X}.", a_armyA,
                     rollA, a_armyB, rollB, winnerId);
        // Perdedor é derrotado (desfeito); vencedor sofre baixas proporcionais ao esforço (sem modelar o combate
        // em detalhe: a resolução detalhada só existe perto do jogador, M21).
        const auto winnerTotal = Find(winnerId) ? Find(winnerId)->TotalCount() : 0.0;
        const auto ratio       = winnerStrength > 0.0 ? std::clamp(loserStrength / winnerStrength, 0.0, 1.0) : 0.0;
        ApplyLosses(winnerId, static_cast<std::uint32_t>(std::lround(winnerTotal * ratio * 0.5)));
        Disband(loserId);
    }

    // ---- Debug --------------------------------------------------------------------------------------------

    void Army::DebugRecruitHere()
    {
        auto t = Conquest::Get().TerritoryAtPlayer(/*onlyCleared*/ false);
        if (!t) {
            if (const auto seat = Seat::Get().SeatTerritory(); seat != kNeutral) {
                t = Config::Get().Find(seat);
            }
        }
        if (!t) {
            I18n::NotifyTr("army.noTerritory");
            return;
        }
        const auto faction = Factions::Get().PlayerFaction();
        if (!faction || State::Get().Owner(t->id) != faction->id) {
            I18n::NotifyTr("army.wrongOwner", t->name);
            return;
        }
        const auto n = Config::Get().Debug().armyRecruitCount;
        const auto r = Recruit(faction->id, t->id, kNeutral, n);
        switch (r) {
        case RecruitResult::kOk:
            break;  // já notificado por Recruit
        case RecruitResult::kNoGold:
            I18n::NotifyTr("army.noGold");
            break;
        case RecruitResult::kNoEquipment:
            I18n::NotifyTr("army.noEquipment");
            break;
        case RecruitResult::kNoPopulation:
            I18n::NotifyTr("army.noPopulation");
            break;
        default:
            I18n::NotifyTr("army.noTroop");
            break;
        }
    }

    void Army::DebugListArmies()
    {
        const auto faction = Factions::Get().PlayerFaction();
        if (!faction) {
            I18n::NotifyTr("army.noFaction");
            return;
        }
        const auto armies = ArmiesOf(faction->id);
        I18n::NotifyTr("army.debugList", armies.size());
        std::size_t shown = 0;
        for (const auto& a : armies) {
            const auto count = static_cast<std::uint32_t>(std::floor(a.TotalCount()));
            logger::info("Exército (debug): 0x{:08X} em '{}', {} soldado(s), força {:.1f}, {} oficial(is), escolta {}{}.",
                         a.id, Config::Get().NameOf(a.territory), count, a.TotalStrength(), a.officers,
                         a.escort ? "sim" : "não",
                         a.destination != kNeutral ? std::format(", marchando p/ '{}' ({} h)",
                                                                  Config::Get().NameOf(a.destination), a.hoursLeft)
                                                    : ""s);
            if (shown++ < 5) {
                I18n::NotifyTr("army.debugArmy", Config::Get().NameOf(a.territory), count,
                               static_cast<int>(a.TotalStrength()));
            }
        }
    }

    // ---- Internos -------------------------------------------------------------------------------------------

    Army::Entity* Army::FindUnlocked(Id a_army) noexcept
    {
        const auto it = std::find_if(armies_.begin(), armies_.end(), [&](const Entity& e) { return e.id == a_army; });
        return it == armies_.end() ? nullptr : &*it;
    }

    const Army::Entity* Army::FindUnlocked(Id a_army) const noexcept
    {
        const auto it = std::find_if(armies_.begin(), armies_.end(), [&](const Entity& e) { return e.id == a_army; });
        return it == armies_.end() ? nullptr : &*it;
    }

    Army::Entity& Army::StationedArmyOf(Id a_faction, Id a_territory)
    {
        for (auto& a : armies_) {
            if (a.faction == a_faction && a.territory == a_territory && a.destination == kNeutral) {
                return a;
            }
        }
        Entity e;
        e.id        = MakeId(std::format("army/{}", serial_++));
        e.faction   = a_faction;
        e.territory = a_territory;
        armies_.push_back(std::move(e));
        return armies_.back();
    }

    // ---- Cosave ---------------------------------------------------------------------------------------------

    void Army::Clear()
    {
        std::scoped_lock l{ lock_ };
        armies_.clear();
        training_.clear();
        serial_ = 1;
    }

    // 'ARMY' v1: u32 nExércitos × { Id id, Id facção, Id território, Id destino, u32 faltam, u32 total,
    //   u32 nUnidades × { Id tropa, f64 contagem }, u32 oficiais, bool escolta },
    //   u32 nTreinos × { Id facção, Id território, Id tropa, u32 contagem, u32 faltam }, u32 próximo serial.
    void Army::Save(SKSE::SerializationInterface* a_intfc) const
    {
        std::scoped_lock l{ lock_ };
        if (armies_.empty() && training_.empty()) {
            return;
        }
        if (!a_intfc->OpenRecord(kRecord, kRecordVersion)) {
            logger::error("Cosave: falha ao abrir o registro ARMY.");
            return;
        }
        a_intfc->WriteRecordData(static_cast<std::uint32_t>(armies_.size()));
        for (const auto& a : armies_) {
            a_intfc->WriteRecordData(a.id);
            a_intfc->WriteRecordData(a.faction);
            a_intfc->WriteRecordData(a.territory);
            a_intfc->WriteRecordData(a.destination);
            a_intfc->WriteRecordData(a.hoursLeft);
            a_intfc->WriteRecordData(a.totalHours);
            a_intfc->WriteRecordData(static_cast<std::uint32_t>(a.units.size()));
            for (const auto& u : a.units) {
                a_intfc->WriteRecordData(u.troop);
                a_intfc->WriteRecordData(u.count);
            }
            a_intfc->WriteRecordData(a.officers);
            a_intfc->WriteRecordData(a.escort);
        }
        a_intfc->WriteRecordData(static_cast<std::uint32_t>(training_.size()));
        for (const auto& t : training_) {
            a_intfc->WriteRecordData(t.faction);
            a_intfc->WriteRecordData(t.territory);
            a_intfc->WriteRecordData(t.troop);
            a_intfc->WriteRecordData(t.count);
            a_intfc->WriteRecordData(t.hoursLeft);
        }
        a_intfc->WriteRecordData(serial_);
    }

    void Army::LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version)
    {
        if (a_version != kRecordVersion) {
            logger::warn("Cosave: versão {} do registro ARMY não suportada; exércitos descartados.", a_version);
            return;
        }
        std::scoped_lock l{ lock_ };
        std::uint32_t    count{};
        a_intfc->ReadRecordData(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            Entity e;
            a_intfc->ReadRecordData(e.id);
            a_intfc->ReadRecordData(e.faction);
            a_intfc->ReadRecordData(e.territory);
            a_intfc->ReadRecordData(e.destination);
            a_intfc->ReadRecordData(e.hoursLeft);
            a_intfc->ReadRecordData(e.totalHours);
            std::uint32_t units{};
            a_intfc->ReadRecordData(units);
            for (std::uint32_t u = 0; u < units; ++u) {
                Unit unit;
                a_intfc->ReadRecordData(unit.troop);
                a_intfc->ReadRecordData(unit.count);
                e.units.push_back(unit);
            }
            a_intfc->ReadRecordData(e.officers);
            a_intfc->ReadRecordData(e.escort);
            if (Factions::Get().Find(e.faction) && Config::Get().Find(e.territory)) {
                armies_.push_back(std::move(e));
            }
        }
        std::uint32_t trainingCount{};
        a_intfc->ReadRecordData(trainingCount);
        for (std::uint32_t i = 0; i < trainingCount; ++i) {
            Training t;
            a_intfc->ReadRecordData(t.faction);
            a_intfc->ReadRecordData(t.territory);
            a_intfc->ReadRecordData(t.troop);
            a_intfc->ReadRecordData(t.count);
            a_intfc->ReadRecordData(t.hoursLeft);
            if (Factions::Get().Find(t.faction) && Config::Get().Find(t.territory)) {
                training_.push_back(t);
            }
        }
        a_intfc->ReadRecordData(serial_);
        logger::info("Cosave: {} exército(s) e {} treino(s) em andamento carregados.", armies_.size(),
                     training_.size());
    }
}
