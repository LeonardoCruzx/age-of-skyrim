#pragma once

#include "AgeOfSkyrimAPI.h"
#include "Core/Ids.h"

namespace AoS::Api
{
    // Implementação de AgeOfSkyrimAPI::IAgeOfSkyrim (include/AgeOfSkyrimAPI.h) — ADR 0032.
    // Outros plugins pedem a interface por mensagem SKSE (kMessageRequestInterface) ou pelo export
    // AoS_GetInterface. A mesma camada atende os natives Papyrus (src/Papyrus) e, depois, o PrismaUI (M11).
    [[nodiscard]] AgeOfSkyrimAPI::IAgeOfSkyrim* Interface() noexcept;

    // Registra o ouvinte de mensagens de qualquer plugin. Chamar em SKSEPluginLoad.
    void RegisterMessaging();

    // Ganchos para sistemas que ainda não existem nesta base. Cada módulo novo registra o seu provedor
    // em kDataLoaded; sem provedor, a API responde "não disponível" (false / 0 / kNeutral).
    //   seat        — M6  (World/Seat*)
    //   treasury    — M12 (Sim/ economia)
    //   tradeRoutes — M13 (Sim/ rotas)
    //   government  — M18 (Realm/ governo)
    struct Providers
    {
        std::function<std::optional<AgeOfSkyrimAPI::SeatInfo>(Id a_faction)> seat;
        std::function<std::int64_t(Id a_faction)>                             treasury;
        std::function<std::uint32_t(Id a_faction)>                            tradeRoutes;
        std::function<Id(Id a_faction)>                                       government;
    };

    void SetSeatProvider(decltype(Providers::seat) a_fn);
    void SetTreasuryProvider(decltype(Providers::treasury) a_fn);
    void SetTradeRouteProvider(decltype(Providers::tradeRoutes) a_fn);
    void SetGovernmentProvider(decltype(Providers::government) a_fn);

    // Consultas usadas também pelos natives Papyrus.
    [[nodiscard]] std::optional<AgeOfSkyrimAPI::SeatInfo> Seat(Id a_faction);
    [[nodiscard]] std::int64_t                            Treasury(Id a_faction);
    [[nodiscard]] std::uint32_t                           TradeRoutes(Id a_faction);
    [[nodiscard]] Id                                      Government(Id a_faction);

    // Troca de dono pedida de fora (API C++ ou Papyrus): respeita settings.json -> api.allowOwnerChanges.
    bool ExternalSetOwner(Id a_territory, Id a_owner, std::string_view a_origin);
}
