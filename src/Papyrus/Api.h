#pragma once

namespace AoS::Papyrus
{
    // Natives do script AgeOfSkyrim.psc (Data/Scripts/Source) — ADR 0032. Facções e territórios são
    // identificados pela CHAVE de texto ("imperial", "player_1", "Skyrim.esm|0x01A2B3"); "" = neutro/nenhum.
    // Consultas que só leem estado protegido por lock rodam direto na thread do Papyrus (callableFromTasklets);
    // o que toca inventário, mundo ou cria facção roda na thread principal (padrão do VM).
    // Chamar em SKSEPluginLoad (registra o callback na interface Papyrus do SKSE).
    void Register();
}
