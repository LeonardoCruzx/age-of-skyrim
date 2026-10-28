#pragma once

#include "Realm/Data.h"

#include <functional>

namespace AoS::Realm
{
    // População numérica por território (M19). Sem atores: só um float por território, indexado como
    // Config::Territories() (O(1)). Simulada em OnHoursPassed. Registro de cosave 'POPL' v1.
    class Population
    {
    public:
        [[nodiscard]] static Population& Get() noexcept;

        static constexpr std::uint32_t kRecord        = Tag("POPL");
        static constexpr std::uint32_t kRecordVersion = 1;

        // Gancho da economia (M12): a facção tem comida no estoque? Sem gancho = suficiente.
        // Chamado uma vez por facção dona por tick, FORA do mutex do reino.
        using FoodProvider = std::function<bool(Id a_faction)>;
        void SetFoodProvider(FoodProvider a_provider);

        // Volta todos os territórios à população inicial (Population/*.json por tipo + overrides).
        void Reset();

        [[nodiscard]] float  Of(Id a_territory) const;       // 0 se desconhecido
        [[nodiscard]] float  Initial(Id a_territory) const;
        [[nodiscard]] float  AtIndex(std::size_t a_index) const;  // sem checagem de faixa além do tamanho
        [[nodiscard]] double Total(Id a_faction) const;      // O(territórios)

        // Mão de obra -> multiplicador de produção: população / inicial, limitado a [manpowerMin, manpowerMax].
        [[nodiscard]] float Manpower(Id a_territory) const;

        // Alistamento (M20): tira até a_count pessoas, sem descer de draftReserve × inicial. Retorna quantas saíram.
        std::uint32_t Draft(Id a_territory, std::uint32_t a_count);

        // Guerra: perda de captureLoss ao trocar de dono.
        void OnCaptured(Id a_territory);

        // Crescimento logístico até inicial × capacityFactor com comida; queda de starvationPerDay sem comida.
        void Tick(float a_hours);

        void Save(SKSE::SerializationInterface* a_intfc) const;
        void LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version);

    private:
        std::vector<float> current_;
        std::vector<float> initial_;
        FoodProvider       food_;
    };
}
