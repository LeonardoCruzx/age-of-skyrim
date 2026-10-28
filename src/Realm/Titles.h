#pragma once

#include "Realm/Data.h"

namespace AoS::Realm
{
    struct FactionStats;

    // Títulos e progressão (M22). Avaliação barata, só em eventos (troca de dono / tick), nunca por frame.
    // Títulos só sobem: perder territórios não rebaixa (evita anúncio "sobe/desce" em guerra) — ADR 0038.
    // Registro de cosave 'TITL' v1.
    class Titles
    {
    public:
        [[nodiscard]] static Titles& Get() noexcept;

        static constexpr std::uint32_t kRecord        = Tag("TITL");
        static constexpr std::uint32_t kRecordVersion = 1;

        // Título atual; sem título ainda -> o de menor rank (nullptr se Titles/*.json vazio).
        [[nodiscard]] const Title* Current(Id a_faction) const;

        // Desbloqueio numérico do título atual ("officers", "governors", "armyCap"...); a_fallback se não declarado.
        [[nodiscard]] float Unlock(Id a_faction, std::string_view a_key, float a_fallback) const;

        // A facção tem pelo menos o título a_required? (kNeutral = sempre)
        [[nodiscard]] bool Meets(Id a_faction, Id a_required) const;

        // Sobe para o maior título cujos marcos a_stats atinge. title = nullptr se não subiu;
        // first = primeira atribuição (título inicial), que não é anunciada.
        struct Promotion
        {
            const Title* title{ nullptr };
            bool         first{ false };
        };
        Promotion Evaluate(Id a_faction, const FactionStats& a_stats);

        void AddModifiers(Id a_faction, ModifierSet& a_io) const;

        void Clear();
        void Save(SKSE::SerializationInterface* a_intfc) const;
        void LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version);

    private:
        std::unordered_map<Id, Id> titles_;  // facção -> título
    };
}
