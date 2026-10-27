#pragma once

#include "Core/Config.h"

namespace AoS::Discovery
{
    // Territórios descobertos a partir dos BGSLocation carregados (vanilla + qualquer mod), pelas keywords
    // vanilla de tipo de local. Roda uma vez em kDataLoaded; nada roda durante o jogo.
    struct Found
    {
        RE::BGSLocation* location{ nullptr };
        std::string      key;                             // "Plugin.esp|0x00ABCD" (estável entre load orders)
        TerritoryType    type{ TerritoryType::kUnknown };
        RE::BGSLocation* hold{ nullptr };                 // primeiro ancestral com LocTypeHold
        bool             hasPosition{ false };
        RE::NiPoint3     position;
        RE::FormID       worldspace{ 0 };
    };

    // "Plugin.esp|0x00ABCD" com o ID local (sem índice de load order; ESL incluso).
    [[nodiscard]] std::string KeyOf(const RE::TESForm* a_form);

    // Tipo, Hold e posição de um local qualquer (também usado para os JSON com "location").
    [[nodiscard]] Found Describe(RE::BGSLocation* a_location);

    // Todos os locais de tipo reconhecido e permitido por a_settings, sem os excluídos e sem
    // sub-locais de outro candidato (ex.: um acampamento dentro de um forte já listado).
    [[nodiscard]] std::vector<Found> Scan(const DiscoverySettings& a_settings);

    // Resumo PASS/FAIL no log (contagens mínimas esperadas num jogo vanilla).
    void LogSummary();

    // debug ligado: <pasta de log do SKSE>/AgeOfSkyrim_territories.json com tudo o que foi descoberto.
    void WriteDump();
}
