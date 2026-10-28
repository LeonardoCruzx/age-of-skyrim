#pragma once

#include "Core/Config.h"

namespace AoS
{
    // Duas facções podem comerciar? Mesma facção, ou em paz/aliança pela diplomacia (M17); nunca neutro.
    [[nodiscard]] bool CanTrade(Id a_factionA, Id a_factionB);

    // Rotas de comércio (M13) entre dois territórios. Simulação fora da tela: no tick da economia cada rota ativa
    // rende ouro à facção dona conforme a distância e o tipo das pontas. Uma rota fica "cortada" (sem renda, mas
    // não é apagada) se algum território inimigo está a menos de blockRadius do segmento entre as pontas (ADR 0028).
    // Configuração em Economy.json -> "routes". Registro de cosave 'ROUT' v1. Thread-safe.
    class Routes
    {
    public:
        struct Route
        {
            Id id{ kNeutral };  // hash de "route/<menor>/<maior>": a mesma dupla nunca repete
            Id a{ kNeutral };
            Id b{ kNeutral };
        };

        struct Status
        {
            Id     owner{ kNeutral };  // dono da ponta A
            bool   active{ false };    // pontas existem e as facções podem comerciar
            bool   blocked{ false };   // passa por território inimigo
            Id     blocker{ kNeutral };
            float  distance{ 0.0f };   // unidades do jogo (XY)
            double goldPerHour{ 0.0 };
        };

        enum class Result : std::uint8_t
        {
            kOk,
            kUnknownTerritory,
            kSameTerritory,
            kNoPosition,      // território sem posição no mapa
            kOtherWorld,      // pontas em worldspaces diferentes
            kCannotTrade,     // donos diferentes (ou neutro)
            kDuplicate,
            kLimit,
            kTooFar,
        };

        // Renda de um tick por facção (Economy credita no recurso de ouro).
        struct Income
        {
            Id     faction{ kNeutral };
            double gold{ 0.0 };
        };

        struct Totals
        {
            std::uint32_t active{ 0 };
            std::uint32_t blocked{ 0 };
            double        gold{ 0.0 };
        };

        [[nodiscard]] static Routes& Get() noexcept;

        static constexpr std::uint32_t kRecord        = Tag("ROUT");
        static constexpr std::uint32_t kRecordVersion = 1;

        // Economy::Load passa o objeto "routes" já mesclado de todos os arquivos.
        void Configure(const nlohmann::json& a_routes);
        [[nodiscard]] Id ResourceKey() const;  // recurso creditado (padrão "gold")

        // API para a mesa de guerra (M11) e o MCM (M10).
        Result                             Create(Id a_territoryA, Id a_territoryB, Id* a_outRoute = nullptr);
        bool                               Remove(Id a_route);
        [[nodiscard]] std::vector<Route>   List() const;
        [[nodiscard]] std::optional<Status> StatusOf(Id a_route) const;
        [[nodiscard]] std::size_t          MaxRoutes() const;

        // Tick (thread principal): renda de a_hours horas por facção.
        [[nodiscard]] std::vector<Income> Accrue(std::uint32_t a_hours, Totals& a_totals) const;

        // Debug: rota entre os 2 territórios mais próximos do jogador com o mesmo dono. Thread principal.
        void DebugCreateNearest();

        void Clear();
        void Save(SKSE::SerializationInterface* a_intfc) const;
        void LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version);

    private:
        [[nodiscard]] Status Evaluate(const Route& a_route) const;  // lock_ já travado
        [[nodiscard]] double TypeMultiplier(TerritoryType a_type) const;

        mutable std::mutex lock_;
        std::vector<Route> routes_;

        // Configuração
        Id            resource_{ MakeId("gold") };
        std::size_t   maxRoutes_{ 8 };
        double        baseGoldPerHour_{ 0.5 };
        double        goldPer10kUnits_{ 0.5 };
        float         maxDistance_{ 0.0f };     // 0 = sem limite
        float         blockRadius_{ 6000.0f };
        std::unordered_map<TerritoryType, double> typeMultiplier_;
    };
}
