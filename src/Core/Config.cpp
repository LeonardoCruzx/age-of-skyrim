#include "Core/Config.h"

namespace AoS
{
    namespace
    {
        const fs::path kRoot = "Data/SKSE/Plugins/AgeOfSkyrim";

        std::optional<nlohmann::json> ReadJson(const fs::path& a_file)
        {
            std::ifstream in(a_file);
            if (!in) {
                return std::nullopt;
            }
            try {
                return nlohmann::json::parse(in, nullptr, true, /*ignore_comments*/ true);
            } catch (const std::exception& e) {
                logger::error("JSON inválido em {}: {}", a_file.string(), e.what());
                return std::nullopt;
            }
        }
    }

    // ---- FormRef --------------------------------------------------------------------------------

    std::optional<FormRef> FormRef::Parse(std::string_view a_text)
    {
        const auto bar = a_text.find('|');
        if (bar == std::string_view::npos || bar == 0) {
            return std::nullopt;
        }
        auto hex = a_text.substr(bar + 1);
        if (hex.starts_with("0x") || hex.starts_with("0X")) {
            hex.remove_prefix(2);
        }
        std::uint32_t value{};
        const auto [ptr, ec] = std::from_chars(hex.data(), hex.data() + hex.size(), value, 16);
        if (ec != std::errc{} || ptr != hex.data() + hex.size()) {
            return std::nullopt;
        }
        return FormRef{ std::string(a_text.substr(0, bar)), value };
    }

    RE::TESObjectREFR* FormRef::Resolve() const
    {
        const auto dh = RE::TESDataHandler::GetSingleton();
        return dh ? dh->LookupForm<RE::TESObjectREFR>(localId, plugin) : nullptr;
    }

    // ---- Config ---------------------------------------------------------------------------------

    Config& Config::Get() noexcept
    {
        static Config instance;
        return instance;
    }

    void Config::RegisterName(Id a_id, const std::string& a_key)
    {
        const auto [it, inserted] = names_.try_emplace(a_id, a_key);
        if (!inserted && _stricmp(it->second.c_str(), a_key.c_str()) != 0) {
            logger::error("Colisão de hash entre '{}' e '{}': renomeie um deles.", it->second, a_key);
        }
    }

    bool Config::Load()
    {
        territories_.clear();
        index_.clear();
        styles_.clear();
        names_.clear();

        LoadSettings(kRoot / "settings.json");

        const auto dir = kRoot / "Territories";
        std::error_code ec;
        std::vector<fs::path> files;
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                files.push_back(entry.path());
            }
        }
        std::sort(files.begin(), files.end());  // ordem determinística entre arquivos
        for (const auto& f : files) {
            LoadTerritoryFile(f);
        }

        logger::info("Config: {} território(s), {} estilo(s) de bandeira.", territories_.size(), styles_.size());
        return !territories_.empty();
    }

    void Config::LoadSettings(const fs::path& a_file)
    {
        const auto json = ReadJson(a_file);
        if (!json) {
            return;  // settings.json é opcional
        }
        if (const auto dbg = json->find("debug"); dbg != json->end() && dbg->is_object()) {
            debug_.enabled = dbg->value("enabled", false);
            debug_.hotkey  = dbg->value("hotkey", 0x44);
            if (const auto t = dbg->value("territory", std::string{}); !t.empty()) {
                debug_.territory = MakeId(t);
            }
        }
    }

    void Config::LoadTerritoryFile(const fs::path& a_file)
    {
        const auto json = ReadJson(a_file);
        if (!json || !json->is_object()) {
            return;
        }

        // Estilos (heráldicas) declarados por este arquivo.
        if (const auto it = json->find("styles"); it != json->end() && it->is_array()) {
            for (const auto& s : *it) {
                if (!s.is_string()) {
                    continue;
                }
                const auto key = s.get<std::string>();
                const auto id  = MakeId(key);
                if (std::find(styles_.begin(), styles_.end(), id) == styles_.end()) {
                    styles_.push_back(id);
                    RegisterName(id, key);
                }
            }
        }

        const auto list = json->find("territories");
        if (list == json->end() || !list->is_array()) {
            return;
        }

        for (const auto& jt : *list) {
            Territory t;
            t.key = jt.value("id", std::string{});
            if (t.key.empty()) {
                logger::warn("{}: território sem 'id', ignorado.", a_file.filename().string());
                continue;
            }
            t.id   = MakeId(t.key);
            t.name = jt.value("name", t.key);
            if (index_.contains(t.id)) {
                logger::warn("Território duplicado '{}', ignorado.", t.key);
                continue;
            }
            RegisterName(t.id, t.key);

            if (const auto owner = jt.value("defaultOwner", std::string{}); !owner.empty()) {
                t.defaultOwner = MakeId(owner);
                if (std::find(styles_.begin(), styles_.end(), t.defaultOwner) == styles_.end()) {
                    logger::warn("'{}': defaultOwner '{}' não é um estilo declarado; usando neutro.", t.key, owner);
                    t.defaultOwner = kNeutral;
                }
            }

            if (const auto b = jt.find("banners"); b != jt.end() && b->is_object()) {
                for (const auto& [styleKey, refs] : b->items()) {
                    const auto styleId = MakeId(styleKey);
                    if (std::find(styles_.begin(), styles_.end(), styleId) == styles_.end()) {
                        logger::warn("'{}': estilo '{}' não declarado em \"styles\"; ignorado.", t.key, styleKey);
                        continue;
                    }
                    BannerSet set;
                    set.style = styleId;
                    for (const auto& r : refs) {
                        const auto parsed = r.is_string() ? FormRef::Parse(r.get<std::string>()) : std::nullopt;
                        if (parsed) {
                            set.refs.push_back(*parsed);
                        } else {
                            logger::warn("'{}/{}': referência inválida (use \"Plugin.esp|0xID\").", t.key, styleKey);
                        }
                    }
                    t.banners.push_back(std::move(set));
                }
            }

            index_.emplace(t.id, territories_.size());
            territories_.push_back(std::move(t));
        }
    }

    const Territory* Config::Find(Id a_territory) const
    {
        const auto it = index_.find(a_territory);
        return it == index_.end() ? nullptr : &territories_[it->second];
    }

    bool Config::IsValidOwner(Id a_owner) const
    {
        return a_owner == kNeutral || std::find(styles_.begin(), styles_.end(), a_owner) != styles_.end();
    }

    std::string_view Config::NameOf(Id a_id) const
    {
        if (a_id == kNeutral) {
            return "neutral";
        }
        const auto it = names_.find(a_id);
        return it == names_.end() ? "?" : std::string_view{ it->second };
    }
}
