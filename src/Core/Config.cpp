#include "Core/Config.h"

#include "Core/I18n.h"

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
        // Idioma antes de tudo: nomes de territórios são escolhidos na leitura.
        I18n::Load(json ? json->value("language", "auto"s) : "auto"s,
                   json ? json->value("textEncoding", "utf8"s) : "utf8"s);
        if (!json) {
            return;  // settings.json é opcional
        }
        if (const auto dbg = json->find("debug"); dbg != json->end() && dbg->is_object()) {
            debug_.enabled = dbg->value("enabled", false);
            debug_.hotkey         = dbg->value("hotkey", 0x44);
            debug_.selfTestHotkey = dbg->value("selfTestHotkey", 0x57);
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
            t.name = jt.contains("name") ? I18n::Pick(jt["name"], t.key) : t.key;  // "texto" ou {"en":..,"pt-br":..}
            if (index_.contains(t.id)) {
                logger::warn("Território duplicado '{}', ignorado.", t.key);
                continue;
            }
            RegisterName(t.id, t.key);

            // Facção dona em jogo novo; validada em State::ResetToDefaults (facções carregam depois).
            if (const auto owner = jt.value("defaultOwner", std::string{}); !owner.empty()) {
                t.defaultOwner = MakeId(owner);
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
                    // Cada item: "Plugin.esp|0xID" (ref pré-colocada) ou
                    // { "place": "Plugin.esp|0xID", "offset": [direita, frente, cima] } (criado em runtime).
                    for (const auto& r : refs) {
                        if (r.is_string()) {
                            if (const auto parsed = FormRef::Parse(r.get<std::string>())) {
                                set.refs.push_back(*parsed);
                                continue;
                            }
                        } else if (r.is_object() && r.contains("place")) {
                            const auto base = FormRef::Parse(r.value("place", std::string{}));
                            const auto off  = r.value("offset", std::vector<float>{});
                            if (base && (off.empty() || off.size() == 3)) {
                                PlacedSpec spec;
                                spec.key  = MakeId(std::format("{}/{}/{}", t.key, styleKey, set.placed.size()));
                                spec.base = *base;
                                if (off.size() == 3) {
                                    spec.offset = { off[0], off[1], off[2] };
                                }
                                set.placed.push_back(std::move(spec));
                                continue;
                            }
                        }
                        logger::warn("'{}/{}': item inválido (use \"Plugin.esp|0xID\" ou {{\"place\": ..., \"offset\": [x,y,z]}}).",
                                     t.key, styleKey);
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

    std::string_view Config::NameOf(Id a_id) const
    {
        if (a_id == kNeutral) {
            return "neutral";
        }
        const auto it = names_.find(a_id);
        return it == names_.end() ? "?" : std::string_view{ it->second };
    }
}
