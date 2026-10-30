#pragma once

#include "Core/Config.h"

#include <array>
#include <atomic>

namespace AoS
{
    // M14: guarnição dos territórios (ADR 0043 / 0044).
    //  - A guarnição é NUMÉRICA (soldados por território, da facção dona) e fica no cosave 'GARR' v1. Território sem
    //    registro usa a guarnição inicial do tipo (Garrison.json -> "initial"), então facções de IA têm guarnição
    //    sem custo nenhum de memória/tempo.
    //  - Só vira atores perto do jogador: quando o marcador do território (worldLocMarker, senão uma bandeira) está
    //    numa célula carregada e a até settings.garrison.spawnRadius. Criação em lotes por frame (AddTask encadeado),
    //    teto GLOBAL settings.garrison.maxActors, base vanilla por estilo de facção (Garrison.json -> "troops"),
    //    outfit aplicado pelo DLL. Ao descarregar a célula: Disable + SetDelete. Mortes reduzem o número.
    //  - Reposição por hora no tick da economia; manutenção (comida/ouro por soldado) via Economy::RegisterUpkeep;
    //    déficit por mais de deficitGraceHours -> deserção gradual.
    // Getters thread-safe; mundo (spawn/despawn) só na thread principal.
    class Garrison
    {
    public:
        [[nodiscard]] static Garrison& Get() noexcept;

        static constexpr std::uint32_t kRecord        = Tag("GARR");
        static constexpr std::uint32_t kRecordVersion = 1;

        // kDataLoaded, depois de Economy/Upgrades/Conquest: lê Garrison.json, resolve as tropas e registra os sinks,
        // o listener de conquista, a manutenção e o tick.
        void Load();

        // ---- consulta (M10/M11/M16/M20) ----
        [[nodiscard]] std::uint32_t Size(Id a_territory) const;      // soldados (número), 0 se neutro
        [[nodiscard]] std::uint32_t Max(Id a_territory) const;       // teto do tipo + melhorias (M15)
        [[nodiscard]] double        Strength(Id a_territory) const;  // força para a resolução numérica (M16)
        [[nodiscard]] std::uint32_t Present(Id a_territory) const;   // atores vivos no mundo agora
        [[nodiscard]] std::uint32_t PresentTotal() const noexcept { return aliveTotal_.load(); }

        // ---- ações (thread-safe; o mundo é atualizado no próximo refresh) ----
        void Set(Id a_territory, std::uint32_t a_soldiers);  // limitado a Max (ex.: resultado de combate, M16)
        void Add(Id a_territory, std::int32_t a_delta);

        // Tick da economia (fora do lock dela): reposição ou deserção.
        void OnHours(std::uint32_t a_hours);

        // Célula carregou / jogador mudou de local: agenda o refresh (coalescido) na thread principal.
        void RequestRefresh();
        void Refresh();

        // Sinks (thread principal ou qualquer; só agendam trabalho se o ator é nosso).
        void OnActorDeath(RE::FormID a_actor);
        void OnActorAttach(RE::FormID a_actor, bool a_attached);

        // Debug (hotkey): mostra a guarnição do território atual e força o spawn perto do jogador.
        void DebugHere();

        void OnPostLoad();
        void Clear();
        void Save(SKSE::SerializationInterface* a_intfc) const;
        void LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version);

    private:
        // Precisa cobrir TODO TerritoryType: Garrison.json pode ter "initial"/"max" por tipo pra qualquer um
        // (LoadFile indexa initial_[tipo]/max_[tipo] direto, sem checar limite).
        static constexpr std::size_t kTypeCount = static_cast<std::size_t>(TerritoryType::kHouse) + 1;

        struct Troop
        {
            std::string                 key;
            RE::TESBoundObject*         actor{ nullptr };  // TESNPC ou TESLevCharacter (FaceGen vanilla pronto)
            RE::BGSOutfit*              outfit{ nullptr };  // nullptr = outfit padrão da base (reaplicado pelo DLL)
            std::vector<RE::TESFaction*> addFactions;
            std::vector<RE::TESFaction*> removeFactions;
        };

        struct Entry
        {
            Id     owner{ kNeutral };
            double soldiers{ 0.0 };  // frações acumulam (reposição/deserção por hora)
        };

        struct Live
        {
            Id   territory{ kNeutral };
            Id   owner{ kNeutral };
            bool dead{ false };
            bool forced{ false };  // debug: não some no refresh, só ao descarregar a célula
        };

        struct Job
        {
            Id            territory{ kNeutral };
            Id            owner{ kNeutral };
            RE::FormID    anchor{ 0 };
            std::uint32_t remaining{ 0 };
            std::uint32_t index{ 0 };  // posição no círculo
            bool          forced{ false };
        };

        void LoadFile(const fs::path& a_file);
        void ResolveTroop(const std::string& a_key, const nlohmann::json& a_json);
        [[nodiscard]] const Troop* TroopFor(Id a_owner) const;

        [[nodiscard]] std::uint32_t Initial(TerritoryType a_type) const;
        [[nodiscard]] std::uint32_t MaxOf(const Territory& a_territory) const;
        [[nodiscard]] double        SizeUnlocked(const Territory& a_territory, Id a_owner) const;  // lock_ travado
        Entry&                      EntryOf(const Territory& a_territory, Id a_owner);               // lock_ travado

        void OnOwnerChanged(const Territory& a_territory);
        void Enqueue(Job a_job);
        void ProcessQueue();
        bool SpawnOne(Job& a_job);
        void Despawn(RE::FormID a_actor);
        void EnsureOutfit(RE::Actor* a_actor, const Troop& a_troop);
        [[nodiscard]] static RE::TESObjectREFR* FindAnchor(const Territory& a_territory);

        mutable std::mutex lock_;

        // Dados (Garrison.json)
        std::array<std::uint32_t, kTypeCount>      initial_{};
        std::array<std::uint32_t, kTypeCount>      max_{};
        double                                     strengthPerSoldier_{ 1.0 };
        double                                     reinforcePerHour_{ 0.25 };
        double                                     desertPerHour_{ 0.25 };
        std::uint32_t                              deficitGraceHours_{ 6 };
        bool                                       aiUpkeep_{ false };
        std::vector<std::pair<Id, double>>         upkeep_;  // recurso -> por soldado por hora
        std::unordered_map<Id, Troop>              troops_;
        RE::TESFaction*                            playerFaction_{ nullptr };

        // Estado (cosave)
        std::unordered_map<Id, Entry>      entries_;
        std::unordered_map<RE::FormID, Live> live_;     // atores criados por nós
        std::vector<RE::FormID>            stale_;      // atores de um save anterior, apagados no pós-load
        std::atomic<std::uint32_t>         aliveTotal_{ 0 };
        std::atomic<bool>                  anyLive_{ false };

        std::vector<Job>  queue_;
        std::atomic<bool> processing_{ false };
        std::atomic<bool> refreshQueued_{ false };
    };
}
