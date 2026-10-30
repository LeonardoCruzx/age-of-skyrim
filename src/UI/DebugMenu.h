#pragma once

// M30: menu de debug (listas vanilla, UI/Prompt) com todas as ações de debug por nome, em categorias — uma
// alternativa à tecla dedicada por ação (útil em teclado sem muitas teclas livres). Mesma função por trás de
// cada tecla e cada item de menu (Debug/DebugActions.h), nunca desalinha.
namespace AoS::DebugMenu
{
    void Open();  // chamar na thread principal (fila do SKSE::GetTaskInterface)
}
