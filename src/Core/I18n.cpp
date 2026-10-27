#include "Core/I18n.h"

namespace AoS::I18n
{
    namespace
    {
        const fs::path kDir = "Data/SKSE/Plugins/AgeOfSkyrim/Translations";

        std::string                                  language_{ "en" };
        bool                                         cp1252_{ false };
        std::unordered_map<std::string, std::string> strings_;   // idioma escolhido
        std::unordered_map<std::string, std::string> fallback_;  // en

        void ReadFile(const std::string& a_lang, std::unordered_map<std::string, std::string>& a_out)
        {
            std::ifstream in(kDir / (a_lang + ".json"));
            if (!in) {
                logger::warn("I18n: tradução '{}' não encontrada.", a_lang);
                return;
            }
            try {
                const auto json = nlohmann::json::parse(in, nullptr, true, /*ignore_comments*/ true);
                for (const auto& [key, value] : json.items()) {
                    if (value.is_string()) {
                        a_out[key] = value.get<std::string>();
                    }
                }
            } catch (const std::exception& e) {
                logger::error("I18n: JSON inválido em {}.json: {}", a_lang, e.what());
            }
        }

        std::string DetectLanguage()
        {
            const auto ini = RE::INISettingCollection::GetSingleton();
            const auto s   = ini ? ini->GetSetting("sLanguage:General") : nullptr;
            const auto v   = s ? std::string_view{ s->GetString() } : std::string_view{};
            // Skyrim não tem PT-BR oficial: quem joga em PT-BR usa o jogo em inglês e escolhe no settings.json.
            return _strnicmp(v.data(), "PORTUGUESE", v.size()) == 0 && !v.empty() ? "pt-br" : "en";
        }

        std::string ToCp1252(const std::string& a_utf8)
        {
            const auto wide = SKSE::stl::utf8_to_utf16(a_utf8);
            if (!wide) {
                return a_utf8;
            }
            const auto len = REX::W32::WideCharToMultiByte(1252, 0, wide->data(), static_cast<int>(wide->size()),
                                                           nullptr, 0, nullptr, nullptr);
            std::string out(static_cast<std::size_t>(len), '\0');
            REX::W32::WideCharToMultiByte(1252, 0, wide->data(), static_cast<int>(wide->size()), out.data(), len,
                                          nullptr, nullptr);
            return out;
        }
    }

    void Load(std::string_view a_language, std::string_view a_encoding)
    {
        language_ = a_language.empty() || a_language == "auto" ? DetectLanguage() : std::string(a_language);
        std::ranges::transform(language_, language_.begin(), [](char c) { return static_cast<char>(std::tolower(c)); });
        cp1252_ = a_encoding == "cp1252";

        strings_.clear();
        fallback_.clear();
        ReadFile("en", fallback_);
        if (language_ != "en") {
            ReadFile(language_, strings_);
        }
        logger::info("I18n: idioma '{}', {} texto(s) + {} em inglês, codificação {}.", language_, strings_.size(),
                     fallback_.size(), cp1252_ ? "cp1252" : "utf8");
    }

    const std::string& Language() noexcept { return language_; }

    std::string Pick(const nlohmann::json& a_field, std::string_view a_fallback)
    {
        if (a_field.is_string()) {
            return a_field.get<std::string>();
        }
        if (a_field.is_object()) {
            for (const auto& lang : { language_, std::string("en") }) {
                if (const auto it = a_field.find(lang); it != a_field.end() && it->is_string()) {
                    return it->get<std::string>();
                }
            }
        }
        return std::string(a_fallback);
    }

    std::string_view Raw(std::string_view a_key)
    {
        const std::string key(a_key);
        if (const auto it = strings_.find(key); it != strings_.end()) {
            return it->second;
        }
        if (const auto it = fallback_.find(key); it != fallback_.end()) {
            return it->second;
        }
        logger::warn("I18n: chave sem tradução '{}'.", a_key);
        return a_key;
    }

    void Notify(const std::string& a_utf8)
    {
        logger::info("[tela] {}", a_utf8);
        const auto text = cp1252_ ? ToCp1252(a_utf8) : a_utf8;
        RE::DebugNotification(text.c_str());
    }
}
