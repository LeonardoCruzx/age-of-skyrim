#pragma once

#include "Core/Config.h"

#include <array>
#include <atomic>
#include <functional>

namespace AoS
{
    // Economia (M12): territórios produzem recursos por hora de jogo conforme o tipo do local e a facção dona.
    // Tabela em Data/SKSE/Plugins/AgeOfSkyrim/Economy.json + Economy/*.json (outros mods podem adicionar arquivos).
    //  - Facções do jogador: a produção vira ITENS REAIS entregues no depósito (baú da sede, M6) via SetDepot.
    //    Sem depósito, acumula em saldo pendente (cosave) e é entregue quando o depósito for definido.
    //  - Facções de IA: só saldo numérico (usado pela IA na M16).
    // Tick orientado a evento (sem polling): ao fechar menus, carregar células e ao fim de sono/espera, compara
    // Calendar::GetHoursPassed com o último tick; horas inteiras passadas são processadas em lote (ADR 0026).
    // Registro de cosave 'ECON' v1.
    class Economy
    {
    public:
        struct Resource
        {
            Id                   id{ kNeutral };
            std::string          key;       // "bread"
            std::string          category;  // categoria de Resources.json ("food") — só informativo/log
            FormRef              item;
            RE::TESBoundObject*  object{ nullptr };  // resolvido no Load
        };

        // Custo de manutenção por hora (M14: guarnições comem comida e recebem ouro).
        struct Upkeep
        {
            Id     faction{ kNeutral };
            Id     resource{ kNeutral };  // MakeId da chave do recurso ("gold")
            double perHour{ 0.0 };
        };
        // Chamado a cada tick (thread principal) para listar os custos atuais. Não chamar a Economy de dentro.
        using UpkeepProvider = std::function<void(std::vector<Upkeep>& a_out)>;

        // Resumo do último tick (debug / UI).
        struct Report
        {
            std::uint32_t                         hours{ 0 };
            std::uint32_t                         producing{ 0 };      // territórios com dono que produziram
            std::vector<std::pair<Id, double>>    playerProduced;      // recurso -> quantidade (facções do jogador)
            std::vector<std::pair<Id, std::int32_t>> delivered;        // recurso -> itens entregues no depósito
            std::uint32_t                         routesActive{ 0 };
            std::uint32_t                         routesBlocked{ 0 };
            double                                routeGold{ 0.0 };    // total de todas as facções
            bool                                  hasDepot{ false };
            bool                                  deficit{ false };    // alguma facção do jogador em déficit
            double                                ms{ 0.0 };
        };

        [[nodiscard]] static Economy& Get() noexcept;

        static constexpr std::uint32_t kRecord        = Tag("ECON");
        static constexpr std::uint32_t kRecordVersion = 1;

        // Chamar em kDataLoaded, depois de Config/Factions/Stock. Lê os JSONs e registra os sinks de tempo (uma vez).
        void Load();

        // ---- Ponto de integração com a sede (M6) ----
        // Baú onde a produção das facções do jogador é entregue (nullptr = sem depósito; acumula pendente).
        // Chamar da thread principal. Entrega o pendente na hora, se houver.
        void                     SetDepot(RE::TESObjectREFR* a_chest);
        [[nodiscard]] RE::FormID Depot() const;

        // ---- Consumo (gancho para M14) ----
        void RegisterUpkeep(std::string_view a_key, UpkeepProvider a_provider);
        void UnregisterUpkeep(std::string_view a_key);
        [[nodiscard]] bool          InDeficit(Id a_faction) const;
        [[nodiscard]] double        Deficit(Id a_faction, Id a_resource) const;  // falta no último tick
        [[nodiscard]] std::uint32_t DeficitHours(Id a_faction) const;           // horas seguidas em déficit

        // ---- M14/M15: ganchos do tick ----
        // Chamado depois de cada tick (thread principal, FORA do lock da economia: pode consultar a Economy) com as
        // horas processadas. Obras (M15) e reposição/deserção da guarnição (M14) penduram aqui (ADR 0026/0046).
        using TickListener = std::function<void(std::uint32_t a_hours)>;
        void AddTickListener(std::string_view a_key, TickListener a_listener);
        // Multiplicador de produção por território (M15: melhoria de renda). Chamado DENTRO do tick, com o lock da
        // economia travado: não chamar a Economy de dentro. Sem provider = 1.0.
        using TerritoryMultiplier = std::function<double(Id a_territory)>;
        void SetTerritoryMultiplier(TerritoryMultiplier a_provider);
        // ---- fim M14/M15 ----

        // Saldo numérico: IA = estoque acumulado; jogador = pendente ainda não entregue no depósito.
        [[nodiscard]] double Balance(Id a_faction, Id a_resource) const;

        [[nodiscard]] const std::vector<Resource>& Resources() const noexcept { return resources_; }
        [[nodiscard]] const Resource*              FindResource(Id a_resource) const;  // O(1)
        [[nodiscard]] Report                       LastReport() const;

        // Eventos baratos chamam isto (qualquer thread): se passou >= 1 hora de jogo, agenda Tick na thread principal.
        void OnTimeEvent();
        // Thread principal: processa as horas inteiras desde o último tick (limitado a maxCatchUpHours).
        std::uint32_t Tick();  // horas processadas (0 = nada); o chamador avisa o reino fora do lock

        // Debug: processa N horas agora (sem mexer no relógio) e mostra o resumo na tela.
        void DebugForceHours(std::uint32_t a_hours);

        void Clear();
        void Save(SKSE::SerializationInterface* a_intfc) const;
        void LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version);

    private:
        static constexpr std::size_t kTypeCount = static_cast<std::size_t>(TerritoryType::kCity) + 1;
        static constexpr double      kUnset     = -1.0;

        struct Ledger
        {
            std::vector<double> balance;  // por índice de resources_
            std::vector<double> deficit;
            std::uint32_t       deficitHours{ 0 };
        };

        void   LoadFile(const fs::path& a_file, nlohmann::json& a_routes);
        void   Process(std::uint32_t a_hours);  // lock_ já travado
        void   NotifyTick(std::uint32_t a_hours);  // M14/M15: lock_ SOLTO
        void   DeliverPending(Report& a_report);  // lock_ já travado
        Ledger& LedgerOf(Id a_faction);           // lock_ já travado
        [[nodiscard]] std::optional<std::size_t> IndexOf(Id a_resource) const;
        [[nodiscard]] double Multiplier(Id a_faction, bool a_player) const;

        mutable std::mutex lock_;

        // Dados (JSON)
        std::vector<Resource>                                   resources_;
        std::unordered_map<Id, std::size_t>                     resourceIndex_;
        std::array<std::vector<std::pair<std::size_t, double>>, kTypeCount> production_;  // tipo -> (recurso, /h)
        std::unordered_map<Id, double>                          factionMultiplier_;
        double                                                  playerMultiplier_{ 1.0 };
        std::uint32_t                                           maxCatchUpHours_{ 168 };

        // Estado (cosave)
        std::atomic<double>                 lastHour_{ kUnset };
        std::atomic<bool>                   queued_{ false };
        RE::FormID                          depot_{ 0 };
        std::unordered_map<Id, Ledger>      ledgers_;

        std::vector<std::pair<std::string, UpkeepProvider>> upkeep_;
        std::vector<std::pair<std::string, TickListener>>   tickListeners_;  // M14/M15
        TerritoryMultiplier                                 territoryMult_;  // M15
        Report                                              last_;
    };
}
