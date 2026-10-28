#include "Sim/Law.h"

#include "Core/Factions.h"
#include "Core/I18n.h"
#include "Core/State.h"
#include "Core/Timer.h"
#include "Sim/Economy.h"
#include "Sim/Stability.h"  // M26: efeito leve da execução na ordem pública (best-effort)
#include "Sim/Upgrades.h"
#include "Stock/Stock.h"
#include "World/Conquest.h"
#include "World/Discovery.h"
#include "World/Seat.h"

namespace AoS
{
    namespace
    {
        bool IsPlayerFaction(Id a_faction)
        {
            const auto f = Factions::Get().Find(a_faction);
            return f && f->player;
        }
    }

    Law& Law::Get() noexcept
    {
        static Law instance;
        return instance;
    }

    void Law::Load()
    {
        ScopedTimer timer{ "Law::Load" };
        const auto& cfg = Config::Get().Law();
        logger::info("Lei: facção de crime {} (reset de recompensa: {}); captura {:.0f}% dos derrotados, teto base {} "
                     "por território, resgate {:.0f} de ouro/prisioneiro.",
                     cfg.touchCrimeFaction ? "ligada" : "desligada (compat, ADR 0005)", cfg.resetBounty,
                     cfg.captureFraction * 100.0, cfg.baseCapacityPerTerritory, cfg.ransomCostPerPrisoner);
    }

    // ---- prisioneiros --------------------------------------------------------------------------------

    std::uint32_t Law::Prisoners(Id a_faction) const
    {
        std::scoped_lock l{ lock_ };
        const auto       it = prisoners_.find(a_faction);
        return it == prisoners_.end() ? 0 : it->second;
    }

    std::uint32_t Law::CapacityUnlocked(Id a_faction) const
    {
        if (a_faction == kNeutral) {
            return 0;
        }
        const auto&   cfg  = Config::Get().Law();
        const auto&   up   = Upgrades::Get();
        const auto&   st   = State::Get();
        std::uint64_t cap  = 0;  // 64 bits: evita estourar com muitos territórios antes do clamp final
        Config::Get().ForEachTerritory([&](const Territory& a_t) {
            if (st.Owner(a_t.id) != a_faction) {
                return;
            }
            cap += cfg.baseCapacityPerTerritory;
            cap += static_cast<std::uint64_t>(std::max(0.0, up.Stat(a_t.id, Stat::kPrisonCapacity)));
        });
        return static_cast<std::uint32_t>(std::min<std::uint64_t>(cap, std::numeric_limits<std::uint32_t>::max()));
    }

    std::uint32_t Law::Capacity(Id a_faction) const
    {
        std::scoped_lock l{ lock_ };
        return CapacityUnlocked(a_faction);
    }

    void Law::CaptureFromCombat(Id a_capturingFaction, Id a_fromFaction, std::uint32_t a_defeatedCount)
    {
        if (a_capturingFaction == kNeutral || a_defeatedCount == 0) {
            return;
        }
        const auto& cfg = Config::Get().Law();
        const auto  wish =
            static_cast<std::uint32_t>(std::floor(static_cast<double>(a_defeatedCount) * cfg.captureFraction));
        if (wish == 0) {
            return;
        }
        std::uint32_t added = 0;
        {
            std::scoped_lock l{ lock_ };
            const auto       cap  = CapacityUnlocked(a_capturingFaction);
            auto&             held = prisoners_[a_capturingFaction];
            const auto        room = cap > held ? cap - held : 0u;
            added                  = std::min(wish, room);
            held += added;
        }
        if (added > 0) {
            logger::info("Lei: '{}' capturou {} prisioneiro(s) de '{}' (de {} derrotado(s)).",
                         Factions::Get().NameOf(a_capturingFaction), added, Factions::Get().NameOf(a_fromFaction),
                         a_defeatedCount);
            if (IsPlayerFaction(a_capturingFaction)) {
                I18n::NotifyTr("law.captured", added, Factions::Get().NameOf(a_fromFaction));
            }
        } else {
            logger::info("Lei: '{}' sem espaço para prisioneiros de '{}' ({} descartado(s): capacidade cheia).",
                         Factions::Get().NameOf(a_capturingFaction), Factions::Get().NameOf(a_fromFaction), wish);
        }
    }

    // ---- ações ----------------------------------------------------------------------------------------

    Law::ActionResult Law::Ransom(Id a_faction, std::uint32_t a_count)
    {
        ScopedTimer timer{ "Law::Ransom" };
        if (a_count == 0) {
            a_count = 1;
        }
        {
            std::scoped_lock l{ lock_ };
            const auto       it = prisoners_.find(a_faction);
            if (it == prisoners_.end() || it->second == 0) {
                return ActionResult::kNone;
            }
            a_count = std::min(a_count, it->second);
        }
        const auto& cfg  = Config::Get().Law();
        const auto  cost = cfg.ransomCostPerPrisoner * a_count;
        if (IsPlayerFaction(a_faction)) {
            // Mesmo padrão de Upgrades::Start: itens reais (ouro) do baú da sede.
            const auto chest = Seat::Get().Chest();
            if (!chest) {
                return ActionResult::kNoSeat;
            }
            auto&      stock = Stock::Get();
            const auto idx   = stock.CategoryIndex("gold");
            if (!idx) {
                logger::warn("Lei: categoria 'gold' não existe em Resources.json; resgate sem custo.");
            } else {
                const auto need    = static_cast<std::int64_t>(std::ceil(cost));
                const auto totals  = stock.CountByCategory(chest);
                if (*idx >= totals.perCategory.size() || totals.perCategory[*idx] < need) {
                    return ActionResult::kInsufficientGold;
                }
                stock.Take(chest, *idx, need);
                if (stock.IsObserved(chest->GetFormID())) {
                    stock.Recount(chest->GetFormID());
                }
            }
        } else if (!Economy::Get().Spend(a_faction, MakeId("gold"), cost)) {
            return ActionResult::kInsufficientGold;
        }
        {
            // Reconfere sob o lock: o pagamento (acima) rodou fora dele, então outra ação pode ter mudado a
            // contagem nesse meio-tempo (ex.: duas hotkeys em sequência rápida); nunca deixa estourar por baixo.
            std::scoped_lock l{ lock_ };
            auto&            n = prisoners_[a_faction];
            a_count            = std::min(a_count, n);
            n -= a_count;
        }
        logger::info("Lei: '{}' resgatou {} prisioneiro(s) por {:.0f} de ouro.", Factions::Get().NameOf(a_faction),
                     a_count, cost);
        if (IsPlayerFaction(a_faction)) {
            I18n::NotifyTr("law.ransomed", a_count, std::format("{:.0f}", cost));
        }
        return ActionResult::kOk;
    }

    Law::ActionResult Law::Execute(Id a_faction, std::uint32_t a_count)
    {
        if (a_count == 0) {
            a_count = 1;
        }
        {
            std::scoped_lock l{ lock_ };
            const auto       it = prisoners_.find(a_faction);
            if (it == prisoners_.end() || it->second == 0) {
                return ActionResult::kNone;
            }
            a_count = std::min(a_count, it->second);
            it->second -= a_count;
        }
        logger::info("Lei: '{}' executou {} prisioneiro(s).", Factions::Get().NameOf(a_faction), a_count);
        // M26: abalo leve e temporário na ordem pública da própria facção (brutalidade custa estabilidade).
        // Best-effort: Stability::ReportUnrest só soma um valor em memória; nunca falha nem bloqueia aqui.
        Stability::Get().ReportUnrest(a_faction, static_cast<double>(a_count));
        if (IsPlayerFaction(a_faction)) {
            I18n::NotifyTr("law.executed", a_count);
        }
        return ActionResult::kOk;
    }

    // ---- facção de crime (bounty) -----------------------------------------------------------------

    void Law::OnTerritoryOwnerChanged(Id a_territory, Id a_oldOwner, Id a_newOwner)
    {
        const auto& cfg = Config::Get().Law();
        if (!cfg.touchCrimeFaction || !cfg.resetBounty || a_oldOwner == a_newOwner) {
            return;
        }
        const auto t = Config::Get().Find(a_territory);
        if (!t || !t->location) {
            return;  // território sem BGSLocation resolvido (ex.: Free Claim): nada a fazer
        }
        const auto found = Discovery::Describe(t->location);
        const auto hold   = found.hold;
        const auto faction = hold ? hold->unreportedCrimeFaction : nullptr;
        if (!faction) {
            logger::info("Lei: '{}' sem Hold/facção de crime resolvida; recompensa não alterada.", t->key);
            return;
        }
        const auto player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return;
        }
        player->ClearAllCrimeGold(faction);
        logger::info("Lei: '{}' trocou de dono; recompensa do jogador zerada na facção de crime '{}' (0x{:08X}).",
                     t->key, faction->GetName(), faction->GetFormID());
    }

    // ---- Debug ----------------------------------------------------------------------------------------

    namespace
    {
        // Mesmo padrão de Garrison/Upgrades DebugHere: território do jogador, senão a sede, senão debug.territory.
        const Territory* DebugTerritory()
        {
            auto t = Conquest::Get().TerritoryAtPlayer(/*onlyCleared*/ false);
            if (!t) {
                if (const auto seat = Seat::Get().SeatTerritory(); seat != kNeutral) {
                    t = Config::Get().Find(seat);
                } else {
                    t = Config::Get().Find(Config::Get().Debug().territory);
                }
            }
            return t;
        }
    }

    void Law::DebugCapture()
    {
        const auto t = DebugTerritory();
        if (!t) {
            I18n::NotifyTr("law.noTerritory");
            return;
        }
        const auto owner = State::Get().Owner(t->id);
        if (owner == kNeutral) {
            I18n::NotifyTr("law.neutral", t->name);
            return;
        }
        // Sem M16/M17 ligados ainda (ou para testar sem esperar um ataque): simula N derrotados do dono atual
        // capturados pela facção do jogador (se o dono já é do jogador, usa a primeira outra facção conhecida,
        // só para exercitar o fluxo — suposição de debug, documentada no ADR 0061).
        auto captor = owner;
        if (IsPlayerFaction(owner)) {
            for (const auto id : Factions::Get().Ids()) {
                if (id != owner) {
                    captor = id;
                    break;
                }
            }
        } else if (const auto mine = Factions::Get().PlayerFaction()) {
            captor = mine->id;
        }
        if (captor == owner) {
            I18n::NotifyTr("law.noCaptor");
            return;
        }
        const auto n = Config::Get().Law().debugCaptureCount;
        CaptureFromCombat(captor, owner, n);
        I18n::NotifyTr("law.debugCaptured", n, Factions::Get().NameOf(captor), Factions::Get().NameOf(owner),
                       Prisoners(captor), Capacity(captor));
    }

    void Law::DebugRansomFirst()
    {
        const auto mine = Factions::Get().PlayerFaction();
        if (!mine) {
            I18n::NotifyTr("law.noPlayerFaction");
            return;
        }
        switch (Ransom(mine->id, 1)) {
        case ActionResult::kOk:
            break;  // Ransom já notifica
        case ActionResult::kNone:
            I18n::NotifyTr("law.none");
            break;
        case ActionResult::kNoSeat:
            I18n::NotifyTr("law.noSeat");
            break;
        case ActionResult::kInsufficientGold:
            I18n::NotifyTr("law.insufficientGold", std::format("{:.0f}", Config::Get().Law().ransomCostPerPrisoner));
            break;
        }
    }

    void Law::DebugExecuteFirst()
    {
        const auto mine = Factions::Get().PlayerFaction();
        if (!mine) {
            I18n::NotifyTr("law.noPlayerFaction");
            return;
        }
        if (Execute(mine->id, 1) == ActionResult::kNone) {
            I18n::NotifyTr("law.none");
        }
    }

    // ---- Cosave --------------------------------------------------------------------------------------------

    void Law::Clear()
    {
        std::scoped_lock l{ lock_ };
        prisoners_.clear();
    }

    // Formato 'LAWS' v1: u32 n × { Id facção, u32 prisioneiros }.
    void Law::Save(SKSE::SerializationInterface* a_intfc) const
    {
        std::scoped_lock l{ lock_ };
        if (prisoners_.empty()) {
            return;
        }
        if (!a_intfc->OpenRecord(kRecord, kRecordVersion)) {
            logger::error("Cosave: falha ao abrir o registro LAWS.");
            return;
        }
        a_intfc->WriteRecordData(static_cast<std::uint32_t>(prisoners_.size()));
        for (const auto& [faction, count] : prisoners_) {
            a_intfc->WriteRecordData(faction);
            a_intfc->WriteRecordData(count);
        }
    }

    void Law::LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version)
    {
        if (a_version != kRecordVersion) {
            logger::warn("Cosave: versão {} do registro LAWS não suportada; prisioneiros zerados.", a_version);
            return;
        }
        std::scoped_lock l{ lock_ };
        std::uint32_t    count{};
        a_intfc->ReadRecordData(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            Id            faction{};
            std::uint32_t n{};
            a_intfc->ReadRecordData(faction);
            a_intfc->ReadRecordData(n);
            prisoners_[faction] = n;
        }
        logger::info("Cosave: prisioneiros de {} facção(ões).", prisoners_.size());
    }
}
