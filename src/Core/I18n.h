#pragma once

namespace AoS::I18n
{
    // Textos para o jogador vêm de Data/SKSE/Plugins/AgeOfSkyrim/Translations/<idioma>.json (UTF-8),
    // com fallback para en.json e, por último, a própria chave. Nunca escrever texto de jogador no código.
    //   a_language: "en", "pt-br" ou "auto" (sLanguage do jogo; o Skyrim não tem PT-BR oficial -> en).
    //   a_encoding: "utf8" (padrão) ou "cp1252", conforme o que o HUD exibir corretamente.
    void Load(std::string_view a_language, std::string_view a_encoding);

    [[nodiscard]] const std::string& Language() noexcept;

    // Escolhe o texto de um campo JSON que pode ser "texto" ou { "en": "...", "pt-br": "..." }.
    [[nodiscard]] std::string Pick(const nlohmann::json& a_field, std::string_view a_fallback);

    [[nodiscard]] std::string_view Raw(std::string_view a_key);

    template <class... Args>
    [[nodiscard]] std::string Tr(std::string_view a_key, const Args&... a_args)
    {
        const auto fmt = Raw(a_key);
        try {
            return std::vformat(fmt, std::make_format_args(a_args...));
        } catch (const std::format_error&) {
            logger::error("I18n: placeholders inválidos em '{}'.", a_key);
            return std::string(a_key);
        }
    }

    // Mostra uma notificação no canto da tela (convertendo para a codificação configurada).
    void Notify(const std::string& a_utf8);

    template <class... Args>
    void NotifyTr(std::string_view a_key, const Args&... a_args)
    {
        Notify(Tr(a_key, a_args...));
    }
}
