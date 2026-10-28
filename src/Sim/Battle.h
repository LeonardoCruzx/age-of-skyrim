#pragma once

#include "Core/Config.h"
#include "Sim/Ai.h"
#include "Sim/Troops.h"

#include <atomic>
#include <functional>

namespace AoS
{
    // M21: materialização de batalhas em tempo real perto do jogador (ADR 0065).
    //  - Gatilhos: um Ai::Attack (M16) chega ao alvo com o jogador perto (gancho Ai::SetNearPlayerHandler) OU
    //    dois Army (M20) de facções em guerra parados no mesmo território com o jogador perto (gancho
    //    Army::SetClashHandler). Fora do alcance (ou settings.json -> battle.realTime=false): NENHUM gancho é
    //    registrado, então a resolução cai automaticamente na numérica já existente (Ai::Resolve / Army
    //    ResolveNumerically) — esta classe nunca duplica essa lógica, só decide SE ela materializa atores.
    //  - Teto GLOBAL de atores PRÓPRIO (settings.json -> battle.maxActors; NÃO soma com garrison.maxActors),
    //    criação em LOTES POR FRAME (AddTask encadeado, mesmo padrão de Garrison::ProcessQueue/SpawnOne);
    //    reforços entram conforme atores morrem, até esgotar a força abstrata de cada lado.
    //  - Atores: ActorBase/outfit de Troops.json (Troops::TroopFor), mesma estratégia de FaceGen vanilla pronto
    //    de Garrison::AutoSoldier; facções de combate PRÓPRIAS (duas TESFaction criadas em runtime com
    //    SetEnemy entre si — nunca as vanilla), posições nos marcadores do território (sem navmesh novo).
    //  - NÃO sobrevive ao save (o comentário de Ai::LoadRecord já previa isso): nenhuma batalha ativa é
    //    persistida; no load, o ataque (Ai) ou os exércitos (Army) envolvidos simplesmente se reavaliam no
    //    próximo tick e, se ainda relevante, materializam de novo.
    class Battle
    {
    public:
        // Ponto de integração OPCIONAL para a M25 (lei/prisioneiros, fora desta missão): chamado ao fim de uma
        // batalha real com a facção derrotada e quantas baixas ela sofreu. Sem handler registrado, nada muda
        // (os prisioneiros não existem enquanto a M25 não existir).
        using CaptureHandler = std::function<void(Id a_losingFaction, Id a_territory, std::uint32_t a_casualties)>;

        [[nodiscard]] static Battle& Get() noexcept;

        // Registro de cosave 'BATL' v1: SÓ para apagar atores de uma batalha que estava em andamento quando o
        // jogo foi salvo (batalhas em si não sobrevivem ao save, ver comentário da classe acima). Sem isso, os
        // soldados temporários ficariam perdidos no mundo (mesma lição "stale_" de Garrison::OnPostLoad).
        static constexpr std::uint32_t kRecord        = Tag("BATL");
        static constexpr std::uint32_t kRecordVersion = 1;

        // kDataLoaded, depois de Config/Factions/Troops/Economy/Ai/Army: cria as duas facções de combate
        // próprias e, se battle.realTime, registra os ganchos do Ai (M16) e do Army (M20) e os sinks de mundo.
        void Load();

        static void SetCaptureHandler(CaptureHandler a_handler);

        // Hotkey de debug (settings.json -> debug.battleTestHotkey): dois grupos pequenos (facção do jogador x
        // uma facção de IA) se enfrentam perto do jogador, sem depender de um Ai::Attack ou Army real.
        void DebugTestBattle();

        // Sinks de mundo (podem vir de qualquer thread; só fazem trabalho se o ator é nosso).
        void OnActorDeath(RE::FormID a_actor);
        void OnActorAttach(RE::FormID a_actor, bool a_attached);
        // Jogador mudou de local / célula carregou: agenda (coalescido) a checagem de abandono na thread principal.
        void RequestCheck();

        // Pós-load: apaga atores de uma batalha que estava em andamento quando o save foi feito (ADR 0065).
        void OnPostLoad();
        void Clear();  // OnRevert: batalhas não sobrevivem a troca de save; só zera o estado em memória
        void Save(SKSE::SerializationInterface* a_intfc) const;
        void LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version);

    private:
        enum class Source : std::uint8_t
        {
            kAiAttack,
            kArmyClash,
            kDebug,
        };

        struct Side
        {
            Id                    faction{ kNeutral };
            const Troops::Troop*  troop{ nullptr };
            std::uint32_t         totalCount{ 0 };  // força abstrata total a representar (soldados)
            std::uint32_t         spawned{ 0 };      // já colocados na fila/mundo (cumulativo, nunca desce)
            std::uint32_t         alive{ 0 };        // atores vivos agora
            std::uint32_t         casualties{ 0 };   // atores mortos nesta batalha
        };

        struct Instance
        {
            Id         id{ kNeutral };
            Id         territory{ kNeutral };
            RE::FormID anchor{ 0 };
            Source     source{ Source::kDebug };
            Id         attackId{ kNeutral };  // Source::kAiAttack
            Id         armyA{ kNeutral };     // Source::kArmyClash
            Id         armyB{ kNeutral };
            Side       a, b;
        };

        struct Job
        {
            Id            instance{ kNeutral };
            bool          sideIsA{ true };
            std::uint32_t remaining{ 0 };
            std::uint32_t index{ 0 };  // posição na formação (lock_ travado ao ler/escrever)
        };

        bool OnAttackArrivedNearPlayer(const Ai::Attack& a_attack);  // vira o Ai::NearPlayerHandler
        bool OnArmiesClashed(Id a_armyA, Id a_armyB);                // vira o Army::ClashHandler

        [[nodiscard]] Side BuildSide(Id a_faction, std::uint32_t a_count, const Troops::Troop* a_troopHint) const;
        Id   StartInstance(Id a_territory, Source a_source, Side a_sideA, Side a_sideB, Id a_attackId, Id a_armyA,
                           Id a_armyB);
        void KickstartSpawns(Id a_instance);
        void Enqueue(Id a_instance, bool a_sideIsA, std::uint32_t a_count);
        void ProcessQueue();
        bool SpawnOne(Job& a_job);
        void Despawn(RE::FormID a_actor);
        [[nodiscard]] static RE::TESObjectREFR* FindAnchor(const Territory& a_territory);
        [[nodiscard]] static RE::NiPoint3        SlotPosition(const RE::TESObjectREFR& a_anchor, bool a_sideIsA,
                                                               std::uint32_t a_index);

        void CheckAbandon();                                  // thread principal (depois do RequestCheck)
        void FinishInstance(Id a_instance, bool a_abandoned);  // thread principal: despawna e aplica o resultado

        mutable std::mutex                                   lock_;
        std::vector<Instance>                                instances_;
        std::unordered_map<RE::FormID, std::pair<Id, bool>>  live_;  // ator -> (instância, lado A?)
        std::vector<Job>                                     queue_;
        std::atomic<std::uint32_t>                           aliveTotal_{ 0 };
        std::atomic<bool>                                    processing_{ false };
        std::atomic<bool>                                    checkQueued_{ false };
        std::atomic<bool>                                    anyLive_{ false };
        std::uint32_t                                         serial_{ 1 };

        RE::TESFaction*                                       factionA_{ nullptr };  // combate própria, lado A
        RE::TESFaction*                                       factionB_{ nullptr };  // combate própria, lado B

        CaptureHandler                                        capture_;
        std::vector<RE::FormID>                               stale_;  // atores do save anterior, p/ OnPostLoad
    };
}
