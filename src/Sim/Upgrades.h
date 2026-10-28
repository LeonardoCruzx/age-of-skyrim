#pragma once

#include "Core/Config.h"

#include <array>
#include <atomic>

namespace AoS
{
    // Status numéricos que as melhorias somam a um território (lidos por M11/M12/M13/M14/M16).
    //  kDefense:     % de bônus na força da guarnição (Garrison::Strength).
    //  kCapacity:    capacidade extra do estoque (informativo por enquanto).
    //  kIncome:      % de bônus na produção do território (Economy, multiplicador por território).
    //  kVision:      alcance de alerta/visão no mapa tático (unidades do jogo; informativo para M11).
    //  kGarrisonMax: soldados a mais no teto da guarnição (Garrison::Max).
    enum class Stat : std::uint8_t
    {
        kDefense,
        kCapacity,
        kIncome,
        kVision,
        kGarrisonMax,
        kCount,
    };
    inline constexpr std::size_t kStatCount = static_cast<std::size_t>(Stat::kCount);

    [[nodiscard]] std::string_view    ToString(Stat a_stat) noexcept;
    [[nodiscard]] std::optional<Stat> ParseStat(std::string_view a_text) noexcept;

    // M15: melhorias de territórios (ADR 0020 / 0045). Dados em Data/SKSE/Plugins/AgeOfSkyrim/Upgrades.json +
    // Upgrades/*.json (outros mods). Cada melhoria tem níveis I → III com custo em ITENS REAIS do baú da sede (por
    // categoria do estoque, M7) e tempo de obra em horas de jogo, conferido no tick por hora da economia (M12).
    // Status numéricos em cache por território (O(1)). Visual por nível: objetos extras criados com Placement::PlaceAt
    // a partir de coordenadas curadas em JSON (sem coordenadas = só status). Registro de cosave 'UPGR' v1.
    // Getters thread-safe; Start/Tick/visuais na thread principal.
    class Upgrades
    {
    public:
        struct LevelSpec
        {
            std::vector<std::pair<std::size_t, std::int64_t>> cost;  // (categoria do Stock, quantidade)
            double                                            hours{ 0.0 };
            std::array<double, kStatCount>                    stats{};  // total do nível (não cumulativo)
        };

        struct Upgrade
        {
            Id                         id{ kNeutral };
            std::string                key;    // "walls"
            std::string                name;   // localizado (JSON {"en","pt-br"})
            std::vector<TerritoryType> types;  // vazio = todos
            std::vector<LevelSpec>     levels;
        };

        struct Construction
        {
            Id           upgrade{ kNeutral };
            std::uint8_t level{ 0 };        // nível que fica pronto
            double       remaining{ 0.0 };  // horas de jogo
            double       total{ 0.0 };
            Id           owner{ kNeutral };  // facção que mandou construir (troca de dono cancela a obra)
        };

        enum class Result : std::uint8_t
        {
            kOk,
            kUnknownTerritory,
            kUnknownUpgrade,
            kWrongType,     // melhoria não serve para este tipo de território
            kNoOwner,       // território neutro
            kMaxLevel,
            kBusy,          // já tem uma obra em andamento
            kNoSeat,        // facção do jogador sem sede (sem baú para pagar)
            kInsufficient,  // itens insuficientes no baú da sede
        };

        [[nodiscard]] static Upgrades& Get() noexcept;

        static constexpr std::uint32_t kRecord        = Tag("UPGR");
        static constexpr std::uint32_t kRecordVersion = 1;

        // kDataLoaded, depois de Stock/Economy: lê os JSONs e pendura o tick e o multiplicador de renda na Economy.
        void Load();

        // ---- consulta (M10/M11/M12/M14/M16) ----
        [[nodiscard]] double                      Stat(Id a_territory, AoS::Stat a_stat) const;  // O(1)
        [[nodiscard]] std::uint8_t                Level(Id a_territory, Id a_upgrade) const;     // 0 = não construída
        [[nodiscard]] bool                        InConstruction(Id a_territory) const;
        [[nodiscard]] std::optional<Construction> CurrentConstruction(Id a_territory) const;
        [[nodiscard]] const Upgrade*              Find(Id a_upgrade) const;  // O(1)
        [[nodiscard]] const std::vector<Upgrade>& All() const noexcept { return upgrades_; }
        [[nodiscard]] bool                        Allows(const Upgrade& a_upgrade, TerritoryType a_type) const;
        // Primeira melhoria (ordem do JSON) que ainda pode subir de nível no território; kNeutral se nenhuma.
        [[nodiscard]] Id                          NextAvailable(Id a_territory) const;
        // Multiplicador da força da guarnição enquanto o território está em obra (mais vulnerável).
        [[nodiscard]] double ConstructionDefenseMultiplier() const noexcept { return constructionDefense_; }
        // Custo do próximo nível em texto ("Materiais 10, Metais 5"), para UI/notificações.
        [[nodiscard]] std::string CostText(const LevelSpec& a_level) const;

        // ---- ações (thread principal) ----
        // Inicia a obra do próximo nível. Facção do jogador paga com itens do baú da sede (a_payCost = false: grátis,
        // para debug); facções de IA não pagam aqui (M16 decide com os saldos da economia).
        Result Start(Id a_territory, Id a_upgrade, bool a_payCost = true);
        // Tick da economia (fora do lock dela): avança as obras e conclui as prontas.
        void OnHours(std::uint32_t a_hours);
        // Célula carregou: cria os objetos visuais pendentes de territórios cujo marcador está carregado.
        void RequestVisuals();
        void ApplyVisuals();

        // Debug (hotkey): inicia a próxima melhoria do território atual (ou da sede / debug.territory).
        void DebugHere();

        void OnPostLoad();
        void Clear();
        void Save(SKSE::SerializationInterface* a_intfc) const;
        void LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version);

    private:
        enum class Anchor : std::uint8_t
        {
            kAuto,    // marcador do local (worldLocMarker), senão uma bandeira do território
            kMarker,
            kBanner,
        };

        struct VisualObject
        {
            Id                  key{ kNeutral };  // "upgr/<território>/<melhoria>/<nível>/<i>" -> cosave
            FormRef             base;
            RE::TESBoundObject* object{ nullptr };
            RE::NiPoint3        offset;  // direita, frente, cima — no referencial da âncora
            RE::NiPoint3        angle;   // radianos (JSON em graus), somado à rotação da âncora em Z
        };

        struct Visual
        {
            Id                        territory{ kNeutral };
            Id                        upgrade{ kNeutral };
            std::uint8_t              level{ 1 };
            Anchor                    anchor{ Anchor::kAuto };
            std::vector<VisualObject> objects;
        };

        struct TerritoryState
        {
            std::unordered_map<Id, std::uint8_t> levels;  // melhoria -> nível
            std::optional<Construction>          construction;
            std::array<double, kStatCount>       stats{};  // cache
        };

        void LoadFile(const fs::path& a_file);
        void Recompute(Id a_territory, TerritoryState& a_state) const;  // lock_ travado
        void MarkVisualsPending(Id a_territory);                          // lock_ travado
        [[nodiscard]] static RE::TESObjectREFR* FindAnchor(const Territory& a_territory, Anchor a_anchor);

        mutable std::mutex lock_;

        // Dados (JSON)
        std::vector<Upgrade>                upgrades_;
        std::unordered_map<Id, std::size_t> index_;
        std::vector<Visual>                 visuals_;
        std::unordered_multimap<Id, std::size_t> visualsByTerritory_;
        double                              constructionDefense_{ 0.75 };

        // Estado (cosave)
        std::unordered_map<Id, TerritoryState> states_;
        std::unordered_map<Id, RE::FormID>     visualRefs_;  // VisualObject::key -> ref criada

        std::unordered_set<Id> pendingVisuals_;  // territórios com objetos a criar quando a âncora carregar
        std::atomic<bool>      anyPending_{ false };
        std::atomic<bool>      queued_{ false };
    };
}
