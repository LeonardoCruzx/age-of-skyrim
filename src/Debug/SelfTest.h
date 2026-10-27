#pragma once

#include "Core/Ids.h"

namespace AoS::SelfTest
{
    // Autoteste do controle de banners (settings.json -> debug). Resultado na tela e no log:
    //   Run()        — hotkey: cria os objetos de teste, passa por todos os donos, confere quais ficam visíveis
    //                  e deixa um dono "esperado" gravado no cosave.
    //   OnPostLoad() — após carregar um save que tem esse registro: confere se o dono e os objetos voltaram.
    // Tudo na thread principal.
    void Run();
    void OnPostLoad();

    inline constexpr std::uint32_t kRecord        = Tag("TEST");
    inline constexpr std::uint32_t kRecordVersion = 1;

    void Clear();
    void Save(SKSE::SerializationInterface* a_intfc);
    void LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version);
}
