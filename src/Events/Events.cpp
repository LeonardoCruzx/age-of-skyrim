#include "Events/Events.h"

#include "Banner/BannerController.h"
#include "Core/I18n.h"
#include "Debug/SelfTest.h"
#include "Stock/Stock.h"
#include "World/Claims.h"

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
                    if (key != dbg.hotkey && key != dbg.selfTestHotkey && key != dbg.stockHotkey &&
                        key != dbg.claimHotkey) {
                        continue;
                    }
                    if (const auto ui = RE::UI::GetSingleton(); ui && ui->GameIsPaused()) {
                        continue;
                    }
                    if (key == dbg.selfTestHotkey) {
                        SKSE::GetTaskInterface()->AddTask([] { SelfTest::Run(); });
                    } else if (key == dbg.stockHotkey) {
                        SKSE::GetTaskInterface()->AddTask([] { Stock::Get().ReportPlayerInventory(); });
                    } else if (key == dbg.claimHotkey) {
                        SKSE::GetTaskInterface()->AddTask([] { Claims::Get().TryClaimHere(); });
                    } else {
                        SKSE::GetTaskInterface()->AddTask([t = dbg.territory] { CycleOwner(t); });
                    }
                }
                return RE::BSEventNotifyControl::kContinue;
            }

        private:
            static void CycleOwner(Id a_territory)
            {
                const auto& cfg = Config::Get();
                const auto* t = cfg.Find(a_territory);
                if (!t) {
                    logger::warn("Debug hotkey: configure 'debug.territory' em settings.json.");
                    return;
                }
                Placement::Get().EnsurePlaced(*t);  // objetos de teste nascem na primeira vez
                std::vector<Id> order{ kNeutral };
                const auto factions = Factions::Get().Ids();
                order.insert(order.end(), factions.begin(), factions.end());

                const auto cur  = State::Get().Owner(a_territory);
                const auto it   = std::find(order.begin(), order.end(), cur);
                const auto next = order[(static_cast<std::size_t>(it - order.begin()) + 1) % order.size()];

                if (SetTerritoryOwner(a_territory, next)) {
                    I18n::NotifyTr("debug.ownerChanged", t->name, Factions::Get().NameOf(next));
                }
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
                logger::info("Debug hotkeys ativas (alternar 0x{:X}, autoteste 0x{:X}, estoque 0x{:X}, claim 0x{:X}).",
                             d.hotkey, d.selfTestHotkey, d.stockHotkey, d.claimHotkey);
            }
        }
    }
}
