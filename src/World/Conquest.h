#pragma once

#include "Core/Config.h"

#include <functional>

namespace AoS
{
    // M5: conquista por "local limpo" (ADR 0009 / 0022). O jogo dispara LocationCleared::Event (sem dados) quando
    // um BGSLocation passa a limpo; olhamos a cadeia de locais do jogador (mais interno primeiro), achamos o
    // território O(1) por Config::FindByLocation e passamos para a facção do jogador via SetTerritoryOwner.
    // Re-spawn de inimigos não devolve o território (nada aqui reage ao local "sujar" de novo).
    // Tudo na thread principal (o sink só agenda uma task).
    class Conquest
    {
    public:
        enum class Source : std::uint8_t
        {
            kCleared,  // evento do jogo
            kDebug,    // hotkey de debug (simula sem limpar)
            kApi,      // outro módulo / outro mod (M9)
        };

        // Chamado depois de cada conquista pelo jogador, na thread principal (ex.: Seat oferece fundar a sede).
        using Listener = std::function<void(const Territory& a_territory, Id a_previousOwner, Source a_source)>;

        [[nodiscard]] static Conquest& Get() noexcept;

        // Registra os sinks (LocationCleared + mudança de local do jogador). Chamar em kDataLoaded.
        void Register();
        void AddListener(Listener a_listener);

        // Facção do jogador; se ainda não existe, cria a padrão (nome traduzido, estilo/cor de settings.conquest).
        std::optional<Id> EnsurePlayerFaction();

        // Território mais interno da cadeia de locais do jogador (nullptr se nenhum). a_onlyCleared: só locais limpos.
        [[nodiscard]] const Territory* TerritoryAtPlayer(bool a_onlyCleared) const;

        // Exclusões de settings.conquest (lista de locais/territórios e de plugins, para mods de guerra civil etc.).
        [[nodiscard]] bool IsExcluded(const Territory& a_territory) const;

        // Passa o território para a facção do jogador (cria a facção padrão se preciso), notifica e chama os
        // listeners. Respeita exclusões e onlyFromNeutral. true se o dono mudou.
        bool ConquerForPlayer(const Territory& a_territory, Source a_source);

        // Debug (hotkey): conquista o território do local atual sem precisar limpar.
        void SimulateHere();

        // Sinks (não chamar direto).
        void OnLocationCleared();
        void OnPlayerLocationChanged(RE::BGSLocation* a_old, RE::BGSLocation* a_new);

    private:
        std::vector<Listener> listeners_;
        RE::BGSLocation*      previous_{ nullptr };  // local de onde o jogador acabou de sair
    };
}
