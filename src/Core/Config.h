#pragma once

#include "Core/Ids.h"

namespace AoS
{
    // "Plugin.esp|0x00ABCD": ID *local* do form (sem o índice de load order).
    struct FormRef
    {
        std::string   plugin;
        RE::FormID    localId{ 0 };

        [[nodiscard]] static std::optional<FormRef> Parse(std::string_view a_text);

        // nullptr se o plugin não está carregado ou o form não é do tipo pedido.
        template <class T = RE::TESObjectREFR>
        [[nodiscard]] T* Resolve() const
        {
            const auto dh = RE::TESDataHandler::GetSingleton();
            return dh ? dh->LookupForm<T>(localId, plugin) : nullptr;
        }
    };

    // Objeto criado pelo DLL em runtime (sem editar células no ESP). A referência criada fica no cosave
    // (ver World/Placement). Fase atual: posição relativa ao jogador no momento da criação (só para teste).
    struct PlacedSpec
    {
        Id          key{ kNeutral };     // "território/estilo/índice" -> chave estável no cosave
        FormRef     base;                // objeto base (static, misc item...)
        RE::NiPoint3 offset;             // x = direita, y = frente, z = cima, relativo ao jogador
    };

    // Banners de UM estilo (heráldica) dentro de UM território. Um forte pode ter várias bandeiras.
    struct BannerSet
    {
        Id                      style{ kNeutral };
        std::vector<FormRef>    refs;    // pré-colocadas num plugin
        std::vector<PlacedSpec> placed;  // criadas em runtime
    };

    // Tipo do local (mapa tático, IA, economia). "tower" só vem do JSON: o jogo não tem keyword de torre.
    enum class TerritoryType : std::uint8_t
    {
        kUnknown,
        kFort,
        kTower,
        kCamp,
        kVillage,
        kCity,
    };

    [[nodiscard]] std::string_view             ToString(TerritoryType a_type) noexcept;
    [[nodiscard]] std::optional<TerritoryType> ParseTerritoryType(std::string_view a_text) noexcept;

    struct Territory
    {
        std::string            key;
        Id                     id{ kNeutral };
        std::string            name;
        Id                     defaultOwner{ kNeutral };  // facção
        std::vector<BannerSet> banners;

        // ---- M4: geografia (auto-descoberta ou JSON) ----
        bool                   discovered{ false };     // criado pela descoberta (não veio de um JSON)
        std::optional<FormRef> locationRef;             // JSON: "location": "Skyrim.esm|0xID"
        RE::BGSLocation*       location{ nullptr };     // resolvido em runtime
        TerritoryType          type{ TerritoryType::kUnknown };
        Id                     province{ kNeutral };    // Hold (LocTypeHold) acima do local
        std::string            provinceName;
        bool                   hasPosition{ false };
        RE::NiPoint3           position;                // marcador do mapa (unidades do jogo)
        RE::FormID             worldspace{ 0 };         // vizinhos só no mesmo worldspace
        std::vector<Id>        neighbors;               // k mais próximos, calculado no load
    };

    // settings.json -> "discovery": territórios a partir dos BGSLocation do jogo (inclui os de outros mods).
    struct DiscoverySettings
    {
        bool                       enabled{ true };
        std::vector<TerritoryType> types{ TerritoryType::kFort, TerritoryType::kCamp, TerritoryType::kVillage,
                                          TerritoryType::kCity };
        std::unordered_set<Id>     exclude;             // MakeId("Plugin.esp|0x00ABCD")
        std::size_t                neighbors{ 4 };
    };

    struct DebugSettings
    {
        bool enabled{ false };
        int  hotkey{ 0x44 };          // DIK_F10: alterna o dono
        int  selfTestHotkey{ 0x57 };  // DIK_F11: autoteste
        int  stockHotkey{ 0x42 };     // DIK_F8: estoque do inventário do jogador por categoria
        int  claimHotkey{ 0x43 };     // DIK_F9: Free Claim (fincar estandarte)
        Id   territory{ kNeutral };
    };

    // settings.json -> "claim": Free Claim (estandarte em local genérico).
    struct ClaimSettings
    {
        FormRef banner{ "Skyrim.esm", 0x012EB7 };  // objeto base do estandarte (padrão: Iron Sword, fincada)
        float   minDistance{ 2048.0f };            // unidades do jogo entre claims (~29 m)
        int     maxClaims{ 10 };
        float   maxSlopeDegrees{ 35.0f };          // inclinação máxima do terreno
        float   forwardDistance{ 150.0f };         // à frente do jogador
    };

    // settings.json -> "api": integração com outros mods (ADR 0032).
    struct ApiSettings
    {
        bool papyrusEvents{ true };      // ModEvents "AoS_*" para scripts Papyrus
        bool allowOwnerChanges{ true };  // outros mods (C++/Papyrus) podem trocar o dono de territórios
    };

    class Config
    {
    public:
        [[nodiscard]] static Config& Get() noexcept;

        // Lê settings.json e todos os Territories/*.json de Data/SKSE/Plugins/AgeOfSkyrim/.
        bool Load();

        [[nodiscard]] const Territory*              Find(Id a_territory) const;
        [[nodiscard]] const Territory*              FindByLocation(const RE::BGSLocation* a_location) const;  // O(1)
        [[nodiscard]] const std::vector<Territory>& Territories() const noexcept { return territories_; }
        [[nodiscard]] const std::vector<Id>&        Styles() const noexcept { return styles_; }
        [[nodiscard]] std::string_view              NameOf(Id a_id) const;  // para logs
        [[nodiscard]] const DebugSettings&          Debug() const noexcept { return debug_; }
        [[nodiscard]] const ClaimSettings&          Claim() const noexcept { return claim_; }
        [[nodiscard]] const DiscoverySettings&      Discovery() const noexcept { return discovery_; }
        [[nodiscard]] const ApiSettings&            Api() const noexcept { return api_; }

    private:
        void LoadSettings(const fs::path& a_file);
        void LoadTerritoryFile(const fs::path& a_file);
        void RegisterName(Id a_id, const std::string& a_key);
        void MergeDiscovered();   // M4: territórios do jogo + vínculo dos JSON com "location"
        void ComputeNeighbors();

        std::vector<Territory>                  territories_;
        std::unordered_map<Id, std::size_t>     index_;
        std::vector<Id>                         styles_;
        std::unordered_map<Id, std::string>     names_;
        DebugSettings                           debug_;
        ClaimSettings                           claim_;
        DiscoverySettings                       discovery_;
        ApiSettings                             api_;
        std::unordered_map<const RE::BGSLocation*, std::size_t> byLocation_;
    };
}
