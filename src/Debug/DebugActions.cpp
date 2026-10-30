#include "Debug/DebugActions.h"

#include "Banner/BannerController.h"
#include "Core/Config.h"
#include "Core/Factions.h"
#include "Core/I18n.h"
#include "Core/State.h"
#include "Debug/SelfTest.h"
#include "Realm/Jarl.h"
#include "Realm/Realm.h"
#include "Sim/Ai.h"
#include "Sim/Army.h"
#include "Sim/Battle.h"
#include "Sim/Diplomacy.h"
#include "Sim/Economy.h"
#include "Sim/Garrison.h"
#include "Sim/Law.h"
#include "Sim/Routes.h"
#include "Sim/Stability.h"
#include "Sim/Upgrades.h"
#include "Stock/Stock.h"
#include "UI/WarMap.h"
#include "World/Claims.h"
#include "World/Conquest.h"
#include "World/Placement.h"
#include "World/Seat.h"

namespace AoS::DebugActions
{
    void CycleOwner()
    {
        const auto& cfg = Config::Get();
        const auto* t   = cfg.Find(cfg.Debug().territory);
        if (!t) {
            logger::warn("Debug hotkey: configure 'debug.territory' em settings.json.");
            return;
        }
        Placement::Get().EnsurePlaced(*t);  // objetos de teste nascem na primeira vez
        std::vector<Id> order{ kNeutral };
        const auto factions = Factions::Get().Ids();
        order.insert(order.end(), factions.begin(), factions.end());

        const auto cur  = State::Get().Owner(t->id);
        const auto it   = std::find(order.begin(), order.end(), cur);
        const auto next = order[(static_cast<std::size_t>(it - order.begin()) + 1) % order.size()];

        if (SetTerritoryOwner(t->id, next)) {
            I18n::NotifyTr("debug.ownerChanged", t->name, Factions::Get().NameOf(next));
        }
    }

    void SelfTest() { AoS::SelfTest::Run(); }  // qualificado: não confundir com AoS::DebugActions::SelfTest (esta função)

    void ReportStock()
    {
        Stock::Get().ReportPlayerInventory();
        Seat::Get().ReportStock();  // M6: estoque da sede, se existir
    }

    void TryClaimHere() { Claims::Get().TryClaimHere(); }
    void SimulateConquest() { Conquest::Get().SimulateHere(); }  // M5
    void EconomyHours() { Economy::Get().DebugForceHours(Config::Get().Debug().economyHours); }
    void CreateRoute() { Routes::Get().DebugCreateNearest(); }
    void OpenWarMap() { WarMap::Get().Open(); }
    void GarrisonHere() { Garrison::Get().DebugHere(); }
    void UpgradeHere() { Upgrades::Get().DebugHere(); }
    void AiTick() { Ai::Get().DebugForceTick(); }
    void DiplomacyCycle() { Diplomacy::Get().DebugCycleHere(); }
    void OfferGovernor() { Realm::Jarl::Get().DebugOfferHere(); }
    void LawCapture() { Law::Get().DebugCapture(); }
    void LawRansom() { Law::Get().DebugRansomFirst(); }
    void LawExecute() { Law::Get().DebugExecuteFirst(); }
    void StabilityHere() { Stability::Get().DebugHere(); }
    void RealmSummary() { Realm::ShowSummary(); }
    void RealmHours() { Realm::DebugSimulateHours(); }
    void RealmDoctrine() { Realm::DebugCycleDoctrine(); }
    void RealmConquerNearby() { Realm::DebugConquerNearby(); }
    void ArmyRecruit() { Army::Get().DebugRecruitHere(); }
    void ArmyList() { Army::Get().DebugListArmies(); }
    void BattleTest() { Battle::Get().DebugTestBattle(); }
}
