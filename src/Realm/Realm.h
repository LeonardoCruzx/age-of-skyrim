#pragma once

#include "Core/Ids.h"

namespace AoS::Realm
{
    // Reino da facção (Fase 4): governo e doutrina (M18), população (M19), títulos (M22).
    // Só números no DLL: zero atores, zero Papyrus, nada por frame. Pontos de integração:
    //   - Realm::Modifiers (Modifiers.h): multiplicadores para economia, recrutamento, defesa...
    //   - OnHoursPassed: a economia (M12) chama a cada tick por hora; aqui não há tick próprio.
    //   - OnTerritoryOwnerChanged: chamado por SetTerritoryOwner quando o dono muda de fato.
    //   - Population::SetFoodProvider / Government::SetPaymentHandler: ganchos que a economia preenche.
    // Thread-safe: todo o estado do reino fica sob um único mutex recursivo (Mutex()).

    [[nodiscard]] std::recursive_mutex& Mutex() noexcept;

    // kDataLoaded, depois de Config::Load e Factions::Load: lê os JSONs, zera a população e liga as hotkeys de debug.
    void Load();

    // Novo jogo / antes de carregar um save (OnRevert): doutrinas, cargos, títulos e população voltam ao padrão.
    void Clear();

    // Relógio do reino em horas de jogo, avançado só por OnHoursPassed (vai no registro GOVT).
    [[nodiscard]] double Clock();

    // Tick estratégico: população (crescimento/fome) e reavaliação de títulos. O(territórios), meta < 2 ms.
    void OnHoursPassed(float a_hours);

    // Gancho de troca de dono (guerra -> população cai, governadores sem maioria saem, títulos reavaliados).
    void OnTerritoryOwnerChanged(Id a_territory, Id a_oldOwner, Id a_newOwner);

    // Números da facção numa passada pelos territórios (O(territórios)): base de títulos e governadores.
    struct FactionStats
    {
        std::uint32_t   territories{ 0 };
        std::uint32_t   cities{ 0 };
        double          population{ 0.0 };
        std::vector<Id> majorityProvinces;  // províncias em que a facção tem a maioria (Data::Governor().majority)
    };
    [[nodiscard]] FactionStats ComputeStats(Id a_faction);

    // M27 (ADR 0057): marco permanente "Dominador(a) de Skyrim" — maioria em dominationThreshold (padrão 1.0,
    // ou seja, todas) das províncias existentes. Avaliado dentro de Reevaluate (troca de dono + tick); exposto
    // ao MCM/outros mods como consulta O(1).
    [[nodiscard]] bool HasDominated(Id a_faction);

    // ---- Debug (settings.json -> debug.realm*). Chamar na thread principal. ----
    void ShowSummary();          // doutrina, população total, título, cargos e modificadores na tela + log
    void DebugSimulateHours();   // OnHoursPassed(debug.realmDebugHours) + resumo
    void DebugCycleDoctrine();   // próxima doutrina (respeita título, custo e espera)
    void DebugConquerNearby();   // N territórios mais próximos -> facção do jogador; preenche cargos/governadores
}
