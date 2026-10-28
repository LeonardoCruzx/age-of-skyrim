#pragma once

#include "Core/Config.h"

namespace AoS
{
    // M26: ordem pública e rebeliões (ADR 0062/0063). Estabilidade (0-100) por território, recomputada a cada
    // tick da economia (pendurado em Economy::AddTickListener, igual Garrison/Upgrades/Ai/Diplomacy — nada de
    // polling). Fontes: doutrina + governador (Realm::Modifiers::GetFor(Realm::Modifier::Stability), multiplicador
    // sobre a base settings.json -> stability.baseline), guarnição insuficiente para a população (Garrison::Strength
    // vs Realm::Population × popToStrength) e déficit de comida OU ouro (Economy::InDeficit). Território abaixo de
    // stability.rebellionThreshold por stability.graceHours seguidas tem stability.chancePerHour de rebelião por
    // hora: vira neutro ou da facção fixa "rebels" (Factions/rebels.json, ADR 0063), com aviso antecipado e de novo
    // se a rebelião acontece. Camada no mapa tático via WarMap::AddProvider("stability", ...) (em src/Main.cpp).
    //  - Estado (horas seguidas instável, aviso já emitido, abalo de execuções do Law) fica só em memória: NÃO há
    //    registro de cosave (ver ADR 0063 para a justificativa de custo/benefício). Pior caso de um load no meio
    //    de uma sequência instável: a contagem reinicia, atrasando no máximo graceHours a próxima rebelião
    //    possível — sem efeito sobre donos, economia ou qualquer outro sistema persistido.
    // Getters thread-safe (cache simples); SetTerritoryOwner (rebelião) e o tick só na thread principal.
    class Stability
    {
    public:
        [[nodiscard]] static Stability& Get() noexcept;

        // kDataLoaded, depois de Config/Factions/Economy/Garrison/Realm (Modifiers precisa de Government/Titles
        // carregados) e ANTES de State::ResetToDefaults (resolve a facção "rebels"). Pendura o tick da economia.
        void Load();

        // ---- consulta (M11 mapa tático, debug) ----
        [[nodiscard]] double        Score(Id a_territory) const;          // 0-100, cache do último tick
        [[nodiscard]] std::uint32_t UnstableHoursOf(Id a_territory) const;  // horas seguidas abaixo do limiar

        // Gancho best-effort do Law (M25): execuções abalam a ordem pública da própria facção por um tempo.
        // Chamado de qualquer thread; nunca falha (se Stability ainda não carregou, é só um no-op).
        void ReportUnrest(Id a_faction, double a_amount);

        // Tick (ouvinte da Economy): recomputa a estabilidade de todos os territórios donos e avalia rebeliões.
        void OnHours(std::uint32_t a_hours);

        // Debug (hotkey): mostra a estabilidade do território atual e força uma checagem de rebelião imediata
        // (ignora graceHours; ainda usa stability.chancePerHour, então pode não rebelar na primeira tentativa).
        void DebugHere();

        // Novo jogo / revert: zera o estado em memória (nada no cosave para carregar).
        void Clear();

    private:
        [[nodiscard]] double ComputeScore(const Territory& a_territory, Id a_owner) const;
        void                 CheckRebellion(const Territory& a_territory, Id a_owner, std::uint32_t a_hours,
                                            bool a_forceRoll);

        mutable std::mutex                    lock_;
        std::unordered_map<Id, double>        scores_;         // território -> última estabilidade computada
        std::unordered_map<Id, std::uint32_t> unstableHours_;  // território -> horas seguidas abaixo do limiar
        std::unordered_set<Id>                warned_;         // território já avisado (evita espamar)
        std::unordered_map<Id, double>        unrest_;         // facção -> abalo extra (execuções), decai por hora
        Id                                    rebelsFaction_{ kNeutral };  // resolvida no Load (Factions/rebels.json)
    };
}
