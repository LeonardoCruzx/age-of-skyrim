#include "Events/Events.h"

#include "Banner/BannerController.h"
#include "Debug/DebugActions.h"  // M30: cada ação de debug mora aqui, não mais inline neste arquivo
#include "UI/DebugMenu.h"        // M30

namespace AoS::Events
{
    namespace
    {
        // Refs não-persistentes só existem quando a célula carrega. Quando isso acontece, tentamos
        // de novo os territórios pendentes. Com refs persistentes (recomendado) isso nunca dispara trabalho.
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
                BannerController::Get().RetryPending();
                return RE::BSEventNotifyControl::kContinue;
            }
        };

        // Ferramentas de teste (settings.json -> debug): uma tecla alterna o dono de um território,
        // outra roda o autoteste. O trabalho vai para a fila da thread principal.
        class DebugHotkeySink final : public RE::BSTEventSink<RE::InputEvent*>
        {
        public:
            static DebugHotkeySink* Get()
            {
                static DebugHotkeySink instance;
                return std::addressof(instance);
            }

            RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const*               a_event,
                                                  RE::BSTEventSource<RE::InputEvent*>*) override
            {
                if (!a_event) {
                    return RE::BSEventNotifyControl::kContinue;
                }
                const auto& dbg = Config::Get().Debug();
                for (auto e = *a_event; e; e = e->next) {
                    if (e->GetEventType() != RE::INPUT_EVENT_TYPE::kButton) {
                        continue;
                    }
                    const auto b = e->AsButtonEvent();
                    if (!b || !b->IsDown() || b->GetDevice() != RE::INPUT_DEVICE::kKeyboard) {
                        continue;
                    }
                    const auto key = static_cast<int>(b->GetIDCode());
                    // M30: cada tecla chama a MESMA função que o item de menu equivalente usa (DebugActions);
                    // uma tabela em vez do if-chain antigo, pra tecla e menu nunca desalinharem.
                    using Action = void (*)();
                    static const std::pair<int, Action> kActions[] = {
                        { dbg.hotkey, +[] { DebugActions::CycleOwner(); } },
                        { dbg.selfTestHotkey, +[] { DebugActions::SelfTest(); } },
                        { dbg.stockHotkey, +[] { DebugActions::ReportStock(); } },
                        { dbg.claimHotkey, +[] { DebugActions::TryClaimHere(); } },
                        { dbg.economyHotkey, +[] { DebugActions::EconomyHours(); } },
                        { dbg.routeHotkey, +[] { DebugActions::CreateRoute(); } },
                        { dbg.warMapHotkey, +[] { DebugActions::OpenWarMap(); } },
                        { dbg.garrisonHotkey, +[] { DebugActions::GarrisonHere(); } },
                        { dbg.upgradeHotkey, +[] { DebugActions::UpgradeHere(); } },
                        { dbg.aiHotkey, +[] { DebugActions::AiTick(); } },
                        { dbg.diplomacyHotkey, +[] { DebugActions::DiplomacyCycle(); } },
                        { dbg.lawCaptureHotkey, +[] { DebugActions::LawCapture(); } },
                        { dbg.lawRansomHotkey, +[] { DebugActions::LawRansom(); } },
                        { dbg.lawExecuteHotkey, +[] { DebugActions::LawExecute(); } },
                        { dbg.stabilityHotkey, +[] { DebugActions::StabilityHere(); } },
                        { dbg.armyRecruitHotkey, +[] { DebugActions::ArmyRecruit(); } },
                        { dbg.armyListHotkey, +[] { DebugActions::ArmyList(); } },
                        { dbg.battleTestHotkey, +[] { DebugActions::BattleTest(); } },
                    };
                    if (key == dbg.menuHotkey) {
                        if (const auto ui = RE::UI::GetSingleton(); ui && ui->GameIsPaused()) {
                            continue;
                        }
                        SKSE::GetTaskInterface()->AddTask([] { DebugMenu::Open(); });
                        continue;
                    }
                    for (const auto& [hk, run] : kActions) {
                        if (key != hk) {
                            continue;
                        }
                        if (const auto ui = RE::UI::GetSingleton(); ui && ui->GameIsPaused()) {
                            break;
                        }
                        SKSE::GetTaskInterface()->AddTask([run] { run(); });
                        break;
                    }
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };
    }

    void Register()
    {
        if (const auto holder = RE::ScriptEventSourceHolder::GetSingleton()) {
            holder->AddEventSink<RE::TESCellFullyLoadedEvent>(CellSink::Get());
        }
        if (Config::Get().Debug().enabled) {
            if (const auto input = RE::BSInputDeviceManager::GetSingleton()) {
                input->AddEventSink(DebugHotkeySink::Get());
                const auto& d = Config::Get().Debug();
                logger::info("Debug hotkeys ativas (alternar 0x{:X}, menu 0x{:X}, autoteste 0x{:X}, estoque 0x{:X}, "
                             "claim 0x{:X}).",
                             d.hotkey, d.menuHotkey, d.selfTestHotkey, d.stockHotkey, d.claimHotkey);
                logger::info("Debug hotkeys M12/M13 (economia 0x{:X} = {} h, rota 0x{:X}).", d.economyHotkey,
                             d.economyHours, d.routeHotkey);
                logger::info("Debug hotkey M11 (mapa tático 0x{:X}).", d.warMapHotkey);
                logger::info("Debug hotkeys M14/M15 (guarnição 0x{:X}, melhoria 0x{:X}, ignorar custo: {}).",
                             d.garrisonHotkey, d.upgradeHotkey, d.upgradeIgnoreCost);
                logger::info("Debug hotkeys M16/M17 (tick da IA 0x{:X}, diplomacia 0x{:X}).", d.aiHotkey,
                             d.diplomacyHotkey);
                logger::info("Debug hotkeys M25/M26 (captura 0x{:X}, resgate 0x{:X}, execução 0x{:X}, "
                             "estabilidade 0x{:X}).",
                             d.lawCaptureHotkey, d.lawRansomHotkey, d.lawExecuteHotkey, d.stabilityHotkey);
                logger::info("Debug hotkeys M20/M21 (recrutar 0x{:X}, listar exércitos 0x{:X}, batalha de teste "
                             "0x{:X}).",
                             d.armyRecruitHotkey, d.armyListHotkey, d.battleTestHotkey);
            }
        }
    }
}
