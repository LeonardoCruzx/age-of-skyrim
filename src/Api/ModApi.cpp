#include "Api/ModApi.h"

#include "Api/Events.h"
#include "Banner/BannerController.h"
#include "Core/Config.h"
#include "Core/Factions.h"
#include "Core/State.h"
#include "Stock/Stock.h"

namespace AoS::Api
{
    namespace
    {
        namespace A = AgeOfSkyrimAPI;

        // Copia texto para o buffer do chamador (sempre com '\0'); retorna o tamanho total do texto.
        std::size_t CopyOut(std::string_view a_text, char* a_buf, std::size_t a_size) noexcept
        {
            if (a_buf && a_size > 0) {
                const auto n = std::min(a_text.size(), a_size - 1);
                std::memcpy(a_buf, a_text.data(), n);
                a_buf[n] = '\0';
            }
            return a_text.size();
        }

        // Structs com campo 'size' no início: escreve só até o tamanho que o chamador conhece (versões antigas).
        template <class T>
        bool CopyStruct(const T& a_src, T* a_dst) noexcept
        {
            constexpr auto kHeader = sizeof(std::uint32_t);
            if (!a_dst || a_dst->size < kHeader) {
                return false;
            }
            const auto n = std::min<std::size_t>(a_dst->size, sizeof(T));
            std::memcpy(reinterpret_cast<char*>(a_dst) + kHeader, reinterpret_cast<const char*>(&a_src) + kHeader,
                        n - kHeader);
            return true;
        }

        std::mutex providersLock_;
        Providers  providers_;

        class ModApi final : public A::IAgeOfSkyrim
        {
        public:
            std::uint32_t GetVersion() const noexcept override { return A::kInterfaceVersion; }

            std::size_t GetPluginVersion(char* a_buf, std::size_t a_size) const noexcept override
            {
                return CopyOut(AOS_VERSION, a_buf, a_size);
            }

            // ---- Facções ----
            std::uint32_t GetFactions(A::FactionId* a_out, std::uint32_t a_capacity) const noexcept override
            {
                try {
                    const auto ids = Factions::Get().Ids();
                    for (std::uint32_t i = 0; a_out && i < a_capacity && i < ids.size(); ++i) {
                        a_out[i] = ids[i];
                    }
                    return static_cast<std::uint32_t>(ids.size());
                } catch (...) {
                    return 0;
                }
            }

            bool GetFactionInfo(A::FactionId a_id, A::FactionInfo* a_out) const noexcept override
            {
                try {
                    const auto f = Factions::Get().Find(a_id);
                    if (!f) {
                        return false;
                    }
                    A::FactionInfo info;
                    info.id       = f->id;
                    info.style    = f->style;
                    info.color    = f->color;
                    info.isPlayer = f->player;
                    return CopyStruct(info, a_out);
                } catch (...) {
                    return false;
                }
            }

            std::size_t GetFactionKey(A::FactionId a_id, char* a_buf, std::size_t a_size) const noexcept override
            {
                try {
                    const auto f = Factions::Get().Find(a_id);
                    return f ? CopyOut(f->key, a_buf, a_size) : CopyOut({}, a_buf, a_size);
                } catch (...) {
                    return 0;
                }
            }

            std::size_t GetFactionName(A::FactionId a_id, char* a_buf, std::size_t a_size) const noexcept override
            {
                try {
                    const auto f = Factions::Get().Find(a_id);
                    return f ? CopyOut(f->name, a_buf, a_size) : CopyOut({}, a_buf, a_size);
                } catch (...) {
                    return 0;
                }
            }

            A::FactionId GetPlayerFaction() const noexcept override
            {
                try {
                    const auto f = Factions::Get().PlayerFaction();
                    return f ? f->id : A::kNone;
                } catch (...) {
                    return A::kNone;
                }
            }

            // ---- Territórios (Config é imutável depois de kDataLoaded: leitura sem lock) ----
            std::uint32_t GetTerritories(A::TerritoryId* a_out, std::uint32_t a_capacity) const noexcept override
            {
                const auto& all = Config::Get().Territories();
                for (std::uint32_t i = 0; a_out && i < a_capacity && i < all.size(); ++i) {
                    a_out[i] = all[i].id;
                }
                return static_cast<std::uint32_t>(all.size());
            }

            bool GetTerritoryInfo(A::TerritoryId a_id, A::TerritoryInfo* a_out) const noexcept override
            {
                const auto* t = Config::Get().Find(a_id);
                if (!t) {
                    return false;
                }
                A::TerritoryInfo info;
                info.id               = t->id;
                info.owner            = State::Get().Owner(t->id);
                info.type             = static_cast<A::TerritoryType>(t->type);
                info.province         = t->province;
                info.locationFormID   = t->location ? t->location->GetFormID() : 0;
                info.worldspaceFormID = t->worldspace;
                info.hasPosition      = t->hasPosition;
                info.position[0]      = t->position.x;
                info.position[1]      = t->position.y;
                info.position[2]      = t->position.z;
                return CopyStruct(info, a_out);
            }

            std::size_t GetTerritoryKey(A::TerritoryId a_id, char* a_buf, std::size_t a_size) const noexcept override
            {
                const auto* t = Config::Get().Find(a_id);
                return CopyOut(t ? std::string_view{ t->key } : std::string_view{}, a_buf, a_size);
            }

            std::size_t GetTerritoryName(A::TerritoryId a_id, char* a_buf, std::size_t a_size) const noexcept override
            {
                const auto* t = Config::Get().Find(a_id);
                return CopyOut(t ? std::string_view{ t->name } : std::string_view{}, a_buf, a_size);
            }

            A::TerritoryId GetTerritoryByLocation(std::uint32_t a_locationFormID) const noexcept override
            {
                const auto loc = RE::TESForm::LookupByID<RE::BGSLocation>(a_locationFormID);
                const auto* t  = loc ? Config::Get().FindByLocation(loc) : nullptr;
                return t ? t->id : A::kNone;
            }

            A::FactionId GetTerritoryOwner(A::TerritoryId a_id) const noexcept override
            {
                return State::Get().Owner(a_id);
            }

            bool SetTerritoryOwner(A::TerritoryId a_id, A::FactionId a_owner) noexcept override
            {
                try {
                    return ExternalSetOwner(a_id, a_owner, "API C++");
                } catch (...) {
                    return false;
                }
            }

            // ---- Estoque ----
            std::uint32_t GetStockCategoryCount() const noexcept override
            {
                return static_cast<std::uint32_t>(Stock::Get().Categories().size());
            }

            std::size_t GetStockCategoryKey(std::uint32_t a_index, char* a_buf, std::size_t a_size) const noexcept override
            {
                const auto& cats = Stock::Get().Categories();
                return CopyOut(a_index < cats.size() ? std::string_view{ cats[a_index].key } : std::string_view{}, a_buf,
                               a_size);
            }

            bool GetStockTotals(std::uint32_t a_container, std::int64_t* a_out, std::uint32_t a_capacity) const noexcept override
            {
                try {
                    const auto totals = Stock::Get().LastTotals(a_container);
                    if (!totals) {
                        return false;
                    }
                    for (std::uint32_t i = 0; a_out && i < a_capacity && i < totals->perCategory.size(); ++i) {
                        a_out[i] = totals->perCategory[i];
                    }
                    return true;
                } catch (...) {
                    return false;
                }
            }

            // ---- Ganchos (M6/M12/M13/M18) ----
            bool GetSeat(A::FactionId a_faction, A::SeatInfo* a_out) const noexcept override
            {
                try {
                    const auto seat = Seat(a_faction);
                    return seat && CopyStruct(*seat, a_out);
                } catch (...) {
                    return false;
                }
            }

            std::int64_t GetTreasury(A::FactionId a_faction) const noexcept override
            {
                try {
                    return Treasury(a_faction);
                } catch (...) {
                    return 0;
                }
            }

            std::uint32_t GetTradeRouteCount(A::FactionId a_faction) const noexcept override
            {
                try {
                    return TradeRoutes(a_faction);
                } catch (...) {
                    return 0;
                }
            }

            std::uint32_t GetGovernment(A::FactionId a_faction) const noexcept override
            {
                try {
                    return Government(a_faction);
                } catch (...) {
                    return A::kNone;
                }
            }

            // ---- Eventos ----
            A::ListenerHandle AddListener(A::EventType a_type, A::EventCallback a_callback, void* a_user) noexcept override
            {
                try {
                    return Events::AddListener(a_type, a_callback, a_user);
                } catch (...) {
                    return 0;
                }
            }

            void RemoveListener(A::ListenerHandle a_handle) noexcept override
            {
                try {
                    Events::RemoveListener(a_handle);
                } catch (...) {
                }
            }
        };

        ModApi instance_;

        void OnMessage(SKSE::MessagingInterface::Message* a_msg)
        {
            if (!a_msg || a_msg->type != A::kMessageRequestInterface) {
                return;
            }
            if (!a_msg->data || a_msg->dataLen < sizeof(A::InterfaceRequest)) {
                logger::warn("API: pedido de interface malformado de '{}'.", a_msg->sender ? a_msg->sender : "?");
                return;
            }
            auto* req   = static_cast<A::InterfaceRequest*>(a_msg->data);
            req->result = req->requestedVersion <= A::kInterfaceVersion ? &instance_ : nullptr;
            logger::info("API: '{}' pediu a interface v{} -> {}.", a_msg->sender ? a_msg->sender : "?",
                         req->requestedVersion, req->result ? "entregue" : "versão não suportada");
        }
    }

    A::IAgeOfSkyrim* Interface() noexcept { return &instance_; }

    void RegisterMessaging()
    {
        // nullptr = mensagens de qualquer plugin (o requisitante não é conhecido de antemão).
        if (SKSE::GetMessagingInterface()->RegisterListener(nullptr, OnMessage)) {
            logger::info("API: interface v{} disponível (mensagem 0x{:08X} para '{}').", A::kInterfaceVersion,
                         A::kMessageRequestInterface, A::kPluginName);
        }
    }

    void SetSeatProvider(decltype(Providers::seat) a_fn)
    {
        std::scoped_lock l{ providersLock_ };
        providers_.seat = std::move(a_fn);
    }

    void SetTreasuryProvider(decltype(Providers::treasury) a_fn)
    {
        std::scoped_lock l{ providersLock_ };
        providers_.treasury = std::move(a_fn);
    }

    void SetTradeRouteProvider(decltype(Providers::tradeRoutes) a_fn)
    {
        std::scoped_lock l{ providersLock_ };
        providers_.tradeRoutes = std::move(a_fn);
    }

    void SetGovernmentProvider(decltype(Providers::government) a_fn)
    {
        std::scoped_lock l{ providersLock_ };
        providers_.government = std::move(a_fn);
    }

    // Cópia da função fora do lock: o provedor pode demorar ou chamar a API.
    std::optional<AgeOfSkyrimAPI::SeatInfo> Seat(Id a_faction)
    {
        decltype(Providers::seat) fn;
        {
            std::scoped_lock l{ providersLock_ };
            fn = providers_.seat;
        }
        return fn ? fn(a_faction) : std::nullopt;
    }

    std::int64_t Treasury(Id a_faction)
    {
        decltype(Providers::treasury) fn;
        {
            std::scoped_lock l{ providersLock_ };
            fn = providers_.treasury;
        }
        return fn ? fn(a_faction) : 0;
    }

    std::uint32_t TradeRoutes(Id a_faction)
    {
        decltype(Providers::tradeRoutes) fn;
        {
            std::scoped_lock l{ providersLock_ };
            fn = providers_.tradeRoutes;
        }
        return fn ? fn(a_faction) : 0;
    }

    Id Government(Id a_faction)
    {
        decltype(Providers::government) fn;
        {
            std::scoped_lock l{ providersLock_ };
            fn = providers_.government;
        }
        return fn ? fn(a_faction) : kNeutral;
    }

    bool ExternalSetOwner(Id a_territory, Id a_owner, std::string_view a_origin)
    {
        if (!Config::Get().Api().allowOwnerChanges) {
            logger::warn("API: troca de dono pedida por {} recusada (api.allowOwnerChanges = false).", a_origin);
            return false;
        }
        logger::info("API: troca de dono pedida por {}.", a_origin);
        return SetTerritoryOwner(a_territory, a_owner);
    }
}

// Alternativa à mensagem SKSE: GetProcAddress(GetModuleHandleW(L"AgeOfSkyrim.dll"), "AoS_GetInterface").
extern "C" __declspec(dllexport) AgeOfSkyrimAPI::IAgeOfSkyrim* AoS_GetInterface(std::uint32_t a_requestedVersion)
{
    return a_requestedVersion <= AgeOfSkyrimAPI::kInterfaceVersion ? AoS::Api::Interface() : nullptr;
}
