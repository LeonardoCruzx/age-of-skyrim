#pragma once

#include "Core/State.h"
#include "World/Placement.h"

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

    // Chama a_fn(ref) para cada bandeira resolvida do conjunto (pré-colocadas + criadas em runtime).
    // Retorna false se alguma pré-colocada ainda não resolve (célula não carregada). Criadas que ainda
    // não existem são puladas sem marcar pendência: só nascem por EnsurePlaced.
    template <class F>
    bool ForEachBanner(const BannerSet& a_set, F&& a_fn)
    {
        bool complete = true;
        for (const auto& ref : a_set.refs) {
            if (const auto r = ref.Resolve()) {
                a_fn(r);
            } else {
                complete = false;
            }
        }
        for (const auto& spec : a_set.placed) {
            if (const auto r = Placement::Get().Find(spec.key)) {
                a_fn(r);
            }
        }
        return complete;
    }

    // API de alto nível para o resto do mod (UI, guerra, debug...). Thread-safe.
    // Atualiza o estado e agenda a troca visual na thread principal.
    bool SetTerritoryOwner(Id a_territory, Id a_owner);
}
