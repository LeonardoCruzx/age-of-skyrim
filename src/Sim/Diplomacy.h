#pragma once

#include "Core/Config.h"

namespace AoS
{
    // M17: relações entre pares de facções (ADR 0049). Padrões data-driven em Factions/*.json:
    //   "defaultRelation": "peace", "playerRelation": "peace",
    //   "relations": [ { "a": "imperial", "b": "stormcloak", "relation": "war" } ]   ("player" = facções do jogador)
    // Mudanças em jogo (declarar guerra, paz aceita, aliança, debug) ficam no cosave 'DIPL' v1 por cima dos padrões.
    // Paz aceita depois de guerra vira TRÉGUA por diplomacy.truceHours e depois paz (sem guerra durante a trégua).
    // Thread-safe. Consultas O(1) (mapa por par).
    class Diplomacy
    {
    public:
        enum class Relation : std::uint8_t
        {
            kPeace,
            kWar,
            kAlliance,
            kTruce,
        };

        enum class Result : std::uint8_t
        {
            kOk,
            kInvalid,        // facção desconhecida, neutra ou a mesma
            kAlreadyAtWar,
            kNotAtWar,
            kInTruce,
            kNotAtPeace,     // aliança só a partir da paz
            kAlreadyAllied,
            kRefused,        // a IA recusou (força/saldo)
        };

        struct Entry
        {
            Id       a{ kNeutral };
            Id       b{ kNeutral };
            Relation relation{ Relation::kPeace };
            double   truceUntil{ 0.0 };  // hora de jogo (Calendar::GetHoursPassed) em que a trégua acaba
        };

        [[nodiscard]] static Diplomacy& Get() noexcept;

        static constexpr std::uint32_t kRecord        = Tag("DIPL");
        static constexpr std::uint32_t kRecordVersion = 1;

        // kDataLoaded, depois de Factions::Load: lê os padrões, registra o ouvinte de conquista e do tick.
        void Load();

        // ---- consulta (IA M16, rotas M13, guardas M14/M21, mapa M11, MCM M10) ----
        [[nodiscard]] Relation RelationOf(Id a_a, Id a_b) const;  // mesma facção / neutro -> paz
        [[nodiscard]] bool     AtWar(Id a_a, Id a_b) const;
        [[nodiscard]] bool     AreHostile(Id a_a, Id a_b) const;  // guardas: só em guerra (M14/M21)
        [[nodiscard]] bool     CanTrade(Id a_a, Id a_b) const;    // mesma facção, paz ou aliança (não trégua)
        [[nodiscard]] double   TruceHoursLeft(Id a_a, Id a_b) const;
        [[nodiscard]] std::vector<Entry> Explicit() const;        // relações que não são a padrão "paz"
        [[nodiscard]] std::vector<Id>    EnemiesOf(Id a_faction) const;

        // ---- ações (MCM / mapa). a_from normalmente é a facção do jogador. Notificam o jogador. ----
        Result DeclareWar(Id a_from, Id a_to);
        Result ProposePeace(Id a_from, Id a_to);     // aceitação calculada por força/saldo
        Result ProposeAlliance(Id a_from, Id a_to);
        // Força a relação (debug / API). a_truceHours só vale para kTruce (0 = diplomacy.truceHours).
        void SetRelation(Id a_a, Id a_b, Relation a_relation, std::uint32_t a_truceHours = 0);

        // Poder usado na aceitação: soma da defesa dos territórios + ouro / ai.goldPerStrength.
        [[nodiscard]] double Power(Id a_faction) const;

        // Debug (NumPad8): alterna paz -> guerra -> trégua -> aliança da facção do jogador com o dono do local atual.
        void DebugCycleHere();

        [[nodiscard]] static std::string_view Key(Relation a_relation) noexcept;  // "diplomacy.rel.*"
        [[nodiscard]] static std::string_view Key(Result a_result) noexcept;      // "diplomacy.result.*"

        // Tick (ouvinte da Economy): trégua vencida vira paz.
        void OnHours(std::uint32_t a_hours);

        void Clear();
        void Save(SKSE::SerializationInterface* a_intfc) const;
        void LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version);

    private:
        [[nodiscard]] static std::uint64_t PairKey(Id a_a, Id a_b) noexcept;
        [[nodiscard]] Relation             RelationUnlocked(Id a_a, Id a_b, double a_now) const;
        void                               SetUnlocked(Id a_a, Id a_b, Relation a_relation, double a_truceUntil);
        void                               LoadFile(const fs::path& a_file);

        mutable std::mutex                          lock_;
        Relation                                    defaultRelation_{ Relation::kPeace };
        Relation                                    playerRelation_{ Relation::kPeace };
        std::unordered_map<std::uint64_t, Entry>    defaults_;   // JSON (ids reais ou "player")
        std::unordered_map<std::uint64_t, Entry>    overrides_;  // em jogo (cosave)
    };
}
