#pragma once

#include "Core/Config.h"

#include <functional>

namespace AoS
{
    // M16: facções de IA e contra-ataques (ADR 0047/0048). Tudo numérico, no DLL, sem atores nem Papyrus.
    //  - Tick estratégico a cada ai.tickHours horas de jogo, pendurado no tick da Economy (ADR 0026).
    //    Cada facção de IA (não-jogador) olha os vizinhos (Territory.neighbors) dos seus territórios e ataca
    //    o de um inimigo EM GUERRA (Diplomacy) — ou neutro, se ai.expandIntoNeutral — quando a força que pode
    //    pagar com o saldo de ouro (Economy::Balance) supera a defesa × ai.minAdvantage.
    //  - Ataque = entidade em marcha (origem, alvo, força, horas restantes), cosave 'WARS' v1. Ao chegar,
    //    resolução por força (± ai.randomness); vitória -> SetTerritoryOwner.
    //  - Perto do jogador (território do jogador): gancho OnAttackArrivedNearPlayer (batalha real na M21);
    //    sem gancho, notifica "X está sob ataque" e resolve numericamente.
    //  - Sede perdida: objetos ficam, depósito da economia suspenso até retomar (ADR 0048).
    // Mutações na thread principal; consultas thread-safe (mapa tático M11, MCM M10).
    class Ai
    {
    public:
        struct Attack
        {
            Id            id{ kNeutral };
            Id            attacker{ kNeutral };  // facção
            Id            origin{ kNeutral };    // território de onde saiu
            Id            target{ kNeutral };    // território alvo
            Id            defender{ kNeutral };  // dono do alvo quando o ataque saiu
            double        strength{ 0.0 };
            std::uint32_t hoursLeft{ 0 };
            std::uint32_t totalHours{ 0 };
            bool          engaged{ false };      // entregue ao gancho (M21) e aguardando ResolveAttack
        };

        // Força de defesa de um território para o dono atual. Padrão: ai.defenseByType (× neutralDefenseMultiplier
        // se neutro). M14/M15 trocam por base + Garrison::Strength + Upgrades::Stat(Defense).
        using DefenseProvider = std::function<double(const Territory& a_territory, Id a_owner)>;
        // Ataque a território do jogador chegou com o jogador por perto. true = o gancho assumiu (M21) e vai
        // chamar ResolveAttack; false = resolução numérica normal.
        using NearPlayerHandler = std::function<bool(const Attack& a_attack)>;

        [[nodiscard]] static Ai& Get() noexcept;

        static constexpr std::uint32_t kRecord        = Tag("WARS");
        static constexpr std::uint32_t kRecordVersion = 1;

        // kDataLoaded, depois de Config/Factions e ANTES de State::ResetToDefaults: lê "aggression"/"start" das
        // facções (Factions/*.json), aplica os donos iniciais, calcula as exclusões e registra os ganchos.
        void Load();

        // ---- integração ----
        void SetDefenseProvider(DefenseProvider a_provider);  // nullptr = volta ao padrão
        void SetNearPlayerHandler(NearPlayerHandler a_handler);
        [[nodiscard]] double DefenseOf(const Territory& a_territory) const;            // dono atual
        [[nodiscard]] double DefenseOf(const Territory& a_territory, Id a_owner) const;
        [[nodiscard]] bool   IsAiFaction(Id a_faction) const;                          // pré-definida (não-jogador)
        [[nodiscard]] bool   CanTarget(const Territory& a_territory) const;            // exclusões (Compat/Guerra Civil)

        // ---- consulta para o mapa (M11) ----
        [[nodiscard]] std::vector<Attack> ActiveAttacks() const;
        [[nodiscard]] std::vector<Attack> Threats(Id a_territory) const;  // ataques em marcha contra o território
        [[nodiscard]] double              HoursToNextTick() const;

        // Gancho da M21: termina um ataque entregue ao NearPlayerHandler. Thread principal.
        bool ResolveAttack(Id a_attack, bool a_attackerWon);
        // Chamado na thread principal quando um ataque a território do jogador chega com ele por perto.
        void OnAttackArrivedNearPlayer(const Attack& a_attack);

        // Tick (ouvinte da Economy): avança marchas, resolve chegadas, avalia a cada tickHours.
        void OnHours(std::uint32_t a_hours);
        // Debug (NumPad7): uma avaliação agora (sem mexer no relógio) e lista os ataques ativos na tela.
        void DebugForceTick();

        // Depois do load: se a sede está ocupada, suspende o depósito (o Seat::OnPostLoad o religa).
        void OnPostLoad();

        void Clear();
        void Save(SKSE::SerializationInterface* a_intfc) const;
        void LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version);

    private:
        struct Summary
        {
            std::uint32_t launched{ 0 };
            std::uint32_t resolved{ 0 };
            std::uint32_t captured{ 0 };
            std::uint32_t evaluated{ 0 };
            std::uint32_t declared{ 0 };
            double        ms{ 0.0 };
        };

        void LoadFactionFile(const fs::path& a_file, std::vector<std::pair<Id, nlohmann::json>>& a_starts);
        void ApplyStarts(const std::vector<std::pair<Id, nlohmann::json>>& a_starts);
        Summary Strategic();                       // thread principal
        void    Advance(std::uint32_t a_hours, Summary& a_summary);
        void    Resolve(Attack a_attack, bool a_attackerWon, Summary* a_summary);
        [[nodiscard]] bool   PlayerNear(const Territory& a_territory) const;
        [[nodiscard]] double Roll(Id a_attack) const;  // fator 1 ± randomness, determinístico por ataque

        mutable std::mutex      lock_;
        DefenseProvider         defense_;
        NearPlayerHandler       nearPlayer_;
        // Montados no Load e imutáveis depois (lidos sem lock, inclusive de dentro da Diplomacy).
        std::unordered_set<Id>         aiFactions_;  // pré-definidas (não-jogador)
        std::unordered_set<Id>         excluded_;    // territórios estáticos fora do alcance da IA
        std::unordered_map<Id, double> aggression_;  // facção -> 0..1

        // Estado (cosave)
        std::vector<Attack>                    attacks_;
        std::unordered_map<Id, std::uint32_t>  cooldown_;  // facção -> horas até poder lançar de novo
        std::uint32_t                          sinceTick_{ 0 };
        std::uint32_t                          serial_{ 1 };
        std::size_t                            cursor_{ 0 };  // lote: de onde a próxima avaliação continua
        bool                                   depotSuspended_{ false };  // sede ocupada (recalculado no load)
    };
}
