#pragma once

#include "Core/State.h"

namespace AoS
{
    // Única classe que toca nos objetos do jogo. SÓ chamar da thread principal
    // (callbacks de mensagem SKSE, eventos do jogo ou SKSE::GetTaskInterface()->AddTask).
    class BannerController
    {
    public:
        [[nodiscard]] static BannerController& Get() noexcept;

        // Habilita os banners do dono atual e desabilita todos os outros do território.
        void ApplyTerritory(Id a_territory);
        void ApplyAll();

        // Territórios cujas referências ainda não puderam ser resolvidas (célula não carregada).
        void RetryPending();

    private:
        static void SetEnabled(RE::TESObjectREFR* a_ref, bool a_enabled);

        std::unordered_set<Id> pending_;
    };

    // API de alto nível para o resto do mod (UI, guerra, debug...). Thread-safe.
    // Atualiza o estado e agenda a troca visual na thread principal.
    bool SetTerritoryOwner(Id a_territory, Id a_owner);
}
