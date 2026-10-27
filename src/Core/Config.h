#pragma once

#include "Core/Ids.h"

namespace AoS
{
    // "Plugin.esp|0x00ABCD": ID *local* da referência (sem o índice de load order).
    struct FormRef
    {
        std::string   plugin;
        RE::FormID    localId{ 0 };

        [[nodiscard]] static std::optional<FormRef> Parse(std::string_view a_text);
        [[nodiscard]] RE::TESObjectREFR*            Resolve() const;  // nullptr se ainda não existe
    };

    // Banners de UM estilo (heráldica) dentro de UM território. Um forte pode ter várias bandeiras.
    struct BannerSet
    {
        Id                   style{ kNeutral };
        std::vector<FormRef> refs;
    };

    struct Territory
    {
        std::string            key;
        Id                     id{ kNeutral };
        std::string            name;
        Id                     defaultOwner{ kNeutral };
        std::vector<BannerSet> banners;
    };

    struct DebugSettings
    {
        bool enabled{ false };
        int  hotkey{ 0x44 };  // DIK_F10
        Id   territory{ kNeutral };
    };

    class Config
    {
    public:
        [[nodiscard]] static Config& Get() noexcept;

        // Lê settings.json e todos os Territories/*.json de Data/SKSE/Plugins/AgeOfSkyrim/.
        bool Load();

        [[nodiscard]] const Territory*              Find(Id a_territory) const;
        [[nodiscard]] const std::vector<Territory>& Territories() const noexcept { return territories_; }
        [[nodiscard]] const std::vector<Id>&        Styles() const noexcept { return styles_; }
        [[nodiscard]] bool                          IsValidOwner(Id a_owner) const;
        [[nodiscard]] std::string_view              NameOf(Id a_id) const;  // para logs
        [[nodiscard]] const DebugSettings&          Debug() const noexcept { return debug_; }

    private:
        void LoadSettings(const fs::path& a_file);
        void LoadTerritoryFile(const fs::path& a_file);
        void RegisterName(Id a_id, const std::string& a_key);

        std::vector<Territory>                  territories_;
        std::unordered_map<Id, std::size_t>     index_;
        std::vector<Id>                         styles_;
        std::unordered_map<Id, std::string>     names_;
        DebugSettings                           debug_;
    };
}
