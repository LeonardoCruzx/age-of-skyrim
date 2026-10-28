#pragma once

#include "Realm/Data.h"

namespace AoS::Realm
{
    struct FactionStats;

    // M27: "Dominador(a) de Skyrim", vitória suave (ADR 0057). Marco booleano PERMANENTE por facção — nunca é
    // perdido, mesmo que a facção depois perca províncias (igual aos títulos, ADR 0038: só sobe).
    // Não é uma entrada de Titles/*.json: a condição é uma FRAÇÃO do total de províncias que existem no save
    // (settings.json -> realm.dominationThreshold), não um marco absoluto como os de TitleRequirements.
    // Avaliação O(1) (um unordered_set), chamada só de dentro de Realm::Reevaluate (troca de dono + tick).
    // Registro de cosave 'DOMN' v1, separado do TITL/GOVT para não arriscar o formato já fechado deles.
    class Domination
    {
    public:
        [[nodiscard]] static Domination& Get() noexcept;

        static constexpr std::uint32_t kRecord        = Tag("DOMN");
        static constexpr std::uint32_t kRecordVersion = 1;

        // true se a facção já alcançou o domínio (permanente, mesmo que hoje tenha menos províncias).
        [[nodiscard]] bool Of(Id a_faction) const;

        // Reaproveita FactionStats::majorityProvinces (mesma lógica de maioria do Government). Devolve true só
        // na hora em que o marco é alcançado (1ª vez) — quem chama decide se anuncia na tela.
        bool Evaluate(Id a_faction, const FactionStats& a_stats);

        void Clear();
        void Save(SKSE::SerializationInterface* a_intfc) const;
        void LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version);

    private:
        mutable std::mutex     lock_;
        std::unordered_set<Id> dominated_;
    };
}
