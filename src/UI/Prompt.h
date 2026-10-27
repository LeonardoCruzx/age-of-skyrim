#pragma once

#include <functional>

namespace AoS::Prompt
{
    // Caixa de diálogo vanilla (MessageBoxData) com botões, sem ESP nem Papyrus.
    // a_onChoice(índice do botão, 0 = primeiro) roda na thread principal; kNoChoice = fechada sem escolha reconhecida.
    // Retorna false se o jogo não ofereceu a fábrica de MessageBoxData (quem chama decide o fallback).
    // Os textos são UTF-8 e convertidos para a codificação do HUD (I18n).
    inline constexpr std::size_t kNoChoice = static_cast<std::size_t>(-1);

    bool Ask(const std::string& a_body, const std::vector<std::string>& a_buttons,
             std::function<void(std::size_t)> a_onChoice);
}
