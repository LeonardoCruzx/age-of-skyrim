#pragma once

namespace AoS
{
    // Identificadores estáveis: o texto do JSON ("whiterun_gate_tower", "stormcloak") é
    // convertido em hash FNV-1a de 32 bits (case-insensitive). O cosave guarda só os hashes,
    // então reordenar/renomear o JSON não corrompe saves (só trocar a *chave* de texto faz).
    using Id = std::uint32_t;

    inline constexpr Id kNeutral = 0;  // sem dono: todos os banners do território ficam desabilitados

    [[nodiscard]] constexpr Id MakeId(std::string_view a_key) noexcept
    {
        std::uint32_t h = 2166136261u;
        for (char c : a_key) {
            if (c >= 'A' && c <= 'Z') {
                c = static_cast<char>(c - 'A' + 'a');
            }
            h ^= static_cast<std::uint8_t>(c);
            h *= 16777619u;
        }
        return h == kNeutral ? 1u : h;  // 0 é reservado para "neutro"
    }

    // Tag de 4 chars para registros do cosave (ex.: 'TERR').
    [[nodiscard]] constexpr std::uint32_t Tag(const char (&s)[5]) noexcept
    {
        return static_cast<std::uint32_t>(s[3]) | (static_cast<std::uint32_t>(s[2]) << 8) |
               (static_cast<std::uint32_t>(s[1]) << 16) | (static_cast<std::uint32_t>(s[0]) << 24);
    }
}
