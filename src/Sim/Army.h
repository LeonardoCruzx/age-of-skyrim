#pragma once

#include "Core/Config.h"

#include <functional>

namespace AoS
{
    // M20: exércitos ABSTRATOS por facção (ADR 0064). Sem atores: só contagens por tipo de tropa (Troops.json),
    // oficiais (bônus simples, sem NPC por ora) e território atual/destino, no cosave 'ARMY' v1. Movem-se entre
    // vizinhos (Territory.neighbors) ao longo de várias horas de jogo (mesma lógica de marcha da IA, M16). Só
    // viram atores perto do jogador na M21 (Battle), via SetClashHandler; longe do jogador (ou sem M21) a
    // resolução de um confronto entre dois exércitos é só numérica (ResolveNumerically).
    // Mutações na thread principal; consultas thread-safe (MCM/mapa, futuro).
    class Army
    {
    public:
        struct Unit
        {
            Id     troop{ kNeutral };  // chave de Troops.json (MakeId)
            double count{ 0.0 };       // fracionário: deserção acumula por hora, como a guarnição (M14)
        };

        struct Entity
        {
            Id                id{ kNeutral };
            Id                faction{ kNeutral };
            Id                territory{ kNeutral };    // onde está agora (parado) ou de onde saiu (em marcha)
            Id                destination{ kNeutral };  // kNeutral = parado
            std::uint32_t     hoursLeft{ 0 };
            std::uint32_t     totalHours{ 0 };
            std::vector<Unit> units;
            std::uint32_t     officers{ 0 };
            bool              escort{ false };  // segue o jogador (M20: só contagem/teto; TODO física, ver ADR 0064)

            [[nodiscard]] double TotalCount() const noexcept;
            [[nodiscard]] double TotalStrength() const;  // soma(count × força do tipo) × (1 + oficiais × bônus)
        };

        enum class RecruitResult
        {
            kOk,
            kDisabled,
            kNoTroop,
            kWrongOwner,
            kNoPopulation,
            kNoGold,
            kNoEquipment,
        };

        // M21 (Battle) registra: dois exércitos de facções em guerra parados no mesmo território, com o jogador
        // por perto. true = a Battle assumiu (chama ApplyLosses/Disband ao fim); false = resolução numérica.
        using ClashHandler = std::function<bool(Id a_armyA, Id a_armyB)>;

        [[nodiscard]] static Army& Get() noexcept;

        static constexpr std::uint32_t kRecord        = Tag("ARMY");
        static constexpr std::uint32_t kRecordVersion = 1;

        // kDataLoaded, depois de Config/Factions/Economy/Troops/Realm::Population/Stock/Seat: registra o tick
        // (Economy::AddTickListener) e a manutenção (Economy::RegisterUpkeep).
        void Load();

        // ---- M20: recrutamento ----
        // a_troop: chave de Troops.json (kNeutral = escolha automática pela facção/estilo, como Garrison::TroopFor).
        // Verifica ouro (Economy::Balance) e equipamento (Stock, se a facção tem sede) ANTES de consumir a
        // população (Realm::Population::Draft pode entregar menos que a_count: o custo final usa o que veio).
        // Sucesso entra na fila de treino (troop.trainHours) e só depois soma ao exército do território.
        RecruitResult Recruit(Id a_faction, Id a_territory, Id a_troop, std::uint32_t a_count);

        // Marcha até um território VIZINHO (Territory.neighbors); horas pela distância (fórmula da IA, M16).
        bool Move(Id a_army, Id a_destination);
        void SetOfficers(Id a_army, std::uint32_t a_officers);  // limitado a army.maxOfficers (settings.json)
        // Entra/sai da escolta do jogador; respeita o teto GLOBAL army.maxEscort (soma de todos os exércitos
        // em escolta da MESMA facção). false = recusado (teto cheio) ou exército inexistente.
        bool SetEscort(Id a_army, bool a_escort);

        [[nodiscard]] std::optional<Entity> Find(Id a_army) const;
        [[nodiscard]] std::vector<Entity>   ArmiesOf(Id a_faction) const;
        [[nodiscard]] std::vector<Entity>   At(Id a_territory) const;  // parados (destination == kNeutral)
        [[nodiscard]] std::uint32_t         EscortCount(Id a_faction) const;

        // ---- M21: integração com a batalha (ver também Ai::SetNearPlayerHandler) ----
        void SetClashHandler(ClashHandler a_handler);  // nullptr = sempre resolução numérica
        // Tira a_losses soldados de a_army (proporcional aos tipos presentes); 0 restante -> Disband. Thread principal.
        void ApplyLosses(Id a_army, std::uint32_t a_losses);
        void Disband(Id a_army);

        // Tick (ouvinte da Economy): treino, marcha, deserção por déficit e detecção de confrontos no destino.
        void OnHours(std::uint32_t a_hours);

        // Debug: recruta army.recruitCount (settings.json -> debug) no território atual da facção do jogador;
        // lista os exércitos de todas as facções do jogador na tela/log.
        void DebugRecruitHere();
        void DebugListArmies();

        void Clear();
        void Save(SKSE::SerializationInterface* a_intfc) const;
        void LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version);

    private:
        struct Training
        {
            Id            faction{ kNeutral };
            Id            territory{ kNeutral };
            Id            troop{ kNeutral };
            std::uint32_t count{ 0 };
            std::uint32_t hoursLeft{ 0 };
        };

        [[nodiscard]] Entity*       FindUnlocked(Id a_army) noexcept;
        [[nodiscard]] const Entity* FindUnlocked(Id a_army) const noexcept;
        Entity&                     StationedArmyOf(Id a_faction, Id a_territory);  // cria se não existir; lock_ travado
        void                        CheckClashes();              // depois de avançar marchas; lock_ SOLTO ao chamar o handler
        void                        ResolveNumerically(Id a_armyA, Id a_armyB);

        mutable std::mutex    lock_;
        std::vector<Entity>   armies_;
        std::vector<Training> training_;
        std::uint32_t         serial_{ 1 };
        ClashHandler          clash_;
    };
}
