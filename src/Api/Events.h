#pragma once

#include "AgeOfSkyrimAPI.h"
#include "Core/Ids.h"

namespace AoS::Api
{
    using Event          = AgeOfSkyrimAPI::Event;
    using EventType      = AgeOfSkyrimAPI::EventType;
    using EventCallback  = AgeOfSkyrimAPI::EventCallback;
    using ListenerHandle = AgeOfSkyrimAPI::ListenerHandle;

    // Registro central de eventos do mod para outros mods (ADR 0032).
    //  - Emit() pode ser chamado de qualquer thread: o evento vai para a fila da thread principal
    //    (SKSE::GetTaskInterface()->AddTask) e lá é entregue, nesta ordem, aos callbacks C++ registrados
    //    pela API (include/AgeOfSkyrimAPI.h) e aos scripts Papyrus (ModEvent "AoS_<Nome>").
    //  - Sem nenhum ouvinte C++ e com Papyrus desligado (settings.json -> api.papyrusEvents), Emit sai na hora.
    //  - Módulos futuros só chamam Emit com o tipo deles (sede M6, tesouro M12, rotas M13, governo M18...).
    namespace Events
    {
        void Emit(const Event& a_event);

        // Atalhos para os eventos que já têm emissor.
        void StateReady();
        void TerritoryChanged(Id a_territory, Id a_owner, Id a_previous);
        void FactionCreated(Id a_faction);
        void FactionChanged(Id a_faction);
        void ClaimCreated(RE::FormID a_banner, std::size_t a_count);
        void StockChanged(RE::FormID a_container);

        [[nodiscard]] ListenerHandle AddListener(EventType a_type, EventCallback a_callback, void* a_user);
        void                         RemoveListener(ListenerHandle a_handle);

        // Nome do ModEvent Papyrus ("AoS_TerritoryChanged"...); vazio se o tipo não existe.
        [[nodiscard]] std::string_view PapyrusName(EventType a_type) noexcept;

        // Dono anterior da última troca de cada território (só em memória; para o Papyrus, que recebe
        // apenas a chave do território no ModEvent). kNeutral se nunca trocou nesta sessão.
        [[nodiscard]] Id PreviousOwner(Id a_territory);
        void             ClearSession();  // novo jogo / antes de carregar um save
    }
}
