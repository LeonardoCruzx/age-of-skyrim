#include "Api/Events.h"

#include "Core/Config.h"
#include "Core/Factions.h"

namespace AoS::Api::Events
{
    namespace
    {
        struct Listener
        {
            ListenerHandle handle{ 0 };
            EventType      type{};
            EventCallback  callback{ nullptr };
            void*          user{ nullptr };
        };

        std::mutex             lock_;
        std::vector<Listener>  listeners_;
        ListenerHandle         nextHandle_{ 1 };
        std::unordered_map<Id, Id> previous_;  // território -> dono anterior

        // ModEvent: strArg = chave do território ou da facção (o que identifica o evento); numArg = valor;
        // sender = form ligado ao evento (container, estandarte) ou None.
        void SendPapyrus(const Event& a_event)
        {
            const auto name = PapyrusName(a_event.type);
            const auto src  = SKSE::GetModCallbackEventSource();
            if (name.empty() || !src) {
                return;
            }
            std::string key;
            if (a_event.territory != kNeutral) {
                const auto* t = Config::Get().Find(a_event.territory);
                key           = t ? t->key : std::format("0x{:08X}", a_event.territory);
            } else if (a_event.faction != kNeutral) {
                const auto f = Factions::Get().Find(a_event.faction);
                key          = f ? f->key : std::format("0x{:08X}", a_event.faction);
            }
            // Eventos de território levam a chave do território; os de facção, a da facção.
            if (a_event.type == EventType::kSeatFounded || a_event.type == EventType::kSeatMoved ||
                a_event.type == EventType::kTradeRouteChanged) {
                const auto f = Factions::Get().Find(a_event.faction);
                key          = f ? f->key : key;
            }
            SKSE::ModCallbackEvent ev{ RE::BSFixedString(name), RE::BSFixedString(key),
                                       static_cast<float>(a_event.value),
                                       a_event.formID ? RE::TESForm::LookupByID(a_event.formID) : nullptr };
            src->SendEvent(&ev);
        }

        void Dispatch(const Event& a_event)  // thread principal
        {
            if (a_event.type == EventType::kTerritoryChanged) {
                std::scoped_lock l{ lock_ };
                previous_[a_event.territory] = a_event.previousFaction;
            }
            std::vector<Listener> copy;
            {
                std::scoped_lock l{ lock_ };
                for (const auto& li : listeners_) {
                    if (li.type == a_event.type) {
                        copy.push_back(li);
                    }
                }
            }
            for (const auto& li : copy) {
                li.callback(a_event, li.user);  // fora do lock: o callback pode chamar a API
            }
            if (Config::Get().Api().papyrusEvents) {
                SendPapyrus(a_event);
            }
        }
    }

    std::string_view PapyrusName(EventType a_type) noexcept
    {
        switch (a_type) {
        case EventType::kStateReady:         return "AoS_StateReady"sv;
        case EventType::kTerritoryChanged:   return "AoS_TerritoryChanged"sv;
        case EventType::kFactionCreated:     return "AoS_FactionCreated"sv;
        case EventType::kFactionChanged:     return "AoS_FactionChanged"sv;
        case EventType::kClaimCreated:       return "AoS_ClaimCreated"sv;
        case EventType::kStockChanged:       return "AoS_StockChanged"sv;
        case EventType::kSeatFounded:        return "AoS_SeatFounded"sv;
        case EventType::kSeatMoved:          return "AoS_SeatMoved"sv;
        case EventType::kTreasuryChanged:    return "AoS_TreasuryChanged"sv;
        case EventType::kTradeRouteChanged:  return "AoS_TradeRouteChanged"sv;
        case EventType::kGovernmentChanged:  return "AoS_GovernmentChanged"sv;
        case EventType::kPopulationChanged:  return "AoS_PopulationChanged"sv;
        case EventType::kTitleGranted:       return "AoS_TitleGranted"sv;
        default:                             return {};
        }
    }

    void Emit(const Event& a_event)
    {
        // TerritoryChanged sempre passa: o dono anterior fica guardado para o Papyrus mesmo sem ouvintes.
        if (a_event.type != EventType::kTerritoryChanged && !Config::Get().Api().papyrusEvents) {
            std::scoped_lock l{ lock_ };
            if (std::none_of(listeners_.begin(), listeners_.end(),
                             [&](const Listener& li) { return li.type == a_event.type; })) {
                return;  // ninguém ouve: custo zero
            }
        }
        if (const auto task = SKSE::GetTaskInterface()) {
            task->AddTask([a_event] { Dispatch(a_event); });
        }
    }

    void StateReady()
    {
        Emit(Event{ .type = EventType::kStateReady });
    }

    void TerritoryChanged(Id a_territory, Id a_owner, Id a_previous)
    {
        Emit(Event{ .type = EventType::kTerritoryChanged, .territory = a_territory, .faction = a_owner,
                    .previousFaction = a_previous });
    }

    void FactionCreated(Id a_faction)
    {
        Emit(Event{ .type = EventType::kFactionCreated, .faction = a_faction });
    }

    void FactionChanged(Id a_faction)
    {
        Emit(Event{ .type = EventType::kFactionChanged, .faction = a_faction });
    }

    void ClaimCreated(RE::FormID a_banner, std::size_t a_count)
    {
        Emit(Event{ .type = EventType::kClaimCreated, .formID = a_banner, .value = static_cast<std::int64_t>(a_count) });
    }

    void StockChanged(RE::FormID a_container)
    {
        Emit(Event{ .type = EventType::kStockChanged, .formID = a_container });
    }

    ListenerHandle AddListener(EventType a_type, EventCallback a_callback, void* a_user)
    {
        if (!a_callback || PapyrusName(a_type).empty()) {
            return 0;
        }
        std::scoped_lock l{ lock_ };
        const auto       handle = nextHandle_++;
        listeners_.push_back({ handle, a_type, a_callback, a_user });
        logger::info("API: ouvinte {} registrado para {}.", handle, PapyrusName(a_type));
        return handle;
    }

    void RemoveListener(ListenerHandle a_handle)
    {
        std::scoped_lock l{ lock_ };
        std::erase_if(listeners_, [&](const Listener& li) { return li.handle == a_handle; });
    }

    Id PreviousOwner(Id a_territory)
    {
        std::scoped_lock l{ lock_ };
        const auto       it = previous_.find(a_territory);
        return it == previous_.end() ? kNeutral : it->second;
    }

    void ClearSession()
    {
        std::scoped_lock l{ lock_ };
        previous_.clear();
    }
}
