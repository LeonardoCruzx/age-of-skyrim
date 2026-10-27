#pragma once

#include "Core/Ids.h"

namespace AoS::SelfTest
{
    // Autoteste do controle de banners (settings.json -> debug). Resultado na tela e no log:
    //   Run()        — hotkey: cria os objetos de teste e uma facção do jogador, passa por todos os donos,
    //                  confere quais bandeiras ficam visíveis e deixa a facção do jogador como dona "esperada".
    //   OnPostLoad() — após carregar um save que tem esse registro: confere se o dono e os objetos voltaram.
    // Tudo na thread principal.
    void Run();
    void OnPostLoad();

    inline constexpr std::uint32_t kRecord        = Tag("TEST");
    inline constexpr std::uint32_t kRecordVersion = 2;  // v2: + facção do jogador

    void Clear();
    void Save(SKSE::SerializationInterface* a_intfc);
    void LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version);
}
