#pragma once

#include "Core/Ids.h"

#include <array>

namespace AoS::Realm
{
    // Modificadores numéricos do reino (M18/M19/M22), lidos pelos outros sistemas:
    //   Production -> economia (M12)        Tax -> impostos sobre a população (M12/M19)
    //   Trade -> rotas (M13)                RecruitCost / RecruitSpeed -> recrutamento (M20)
    //   PopulationGrowth -> população (M19) Defense -> guarnição/batalhas (M14/M16/M21)
    //   Upkeep -> manutenção de guarnições e exércitos (M12/M14/M20)
    //   Stability -> ordem pública/rebeliões (M26, src/Sim/Stability.*): multiplica uma base de estabilidade
    //   por território (doutrina + governador aqui; guarnição/população/déficit somados depois, fora do Modifiers).
    // Nos JSONs cada fonte dá um bônus ADITIVO (0.10 = +10%, -0.05 = -5%); o multiplicador final é
    // 1 + soma das fontes, nunca abaixo de kMinMultiplier.
    // Stability adicionado no FIM do enum (M25/M26): ModifierSet nunca é serializado no cosave (Government/Titles
    // só persistem IDs de doutrina/cargo/título e recalculam o ModifierSet em runtime a partir do JSON atual —
    // ver Government::AddModifiers/Titles::AddModifiers), então crescer o enum aqui não quebra saves antigos.
    enum class Modifier : std::uint8_t
    {
        Production,
        Tax,
        Trade,
        RecruitCost,
        RecruitSpeed,
        PopulationGrowth,
        Defense,
        Upkeep,
        Stability,  // M26: aditivo, no fim
        kCount
    };

    inline constexpr std::size_t kModifierCount = static_cast<std::size_t>(Modifier::kCount);
    inline constexpr float       kMinMultiplier = 0.1f;

    using ModifierSet = std::array<float, kModifierCount>;  // bônus aditivos por modificador

    [[nodiscard]] std::string_view        ToString(Modifier a_modifier) noexcept;  // chave do JSON ("production")
    [[nodiscard]] std::optional<Modifier> ParseModifier(std::string_view a_text) noexcept;

    // Lê { "production": 0.1, "tax": -0.05 } somando em a_out; chaves desconhecidas = aviso (a_context no log).
    void ParseModifiers(const nlohmann::json& a_json, ModifierSet& a_out, std::string_view a_context);

    // Consulta agregada (doutrina + cargos + títulos [+ governador da província + mão de obra]). Thread-safe, O(1)
    // na prática (cargos ≤ poucos por facção). Facção neutra/desconhecida -> 1.0.
    struct Modifiers
    {
        // Bônus da facção inteira: doutrina + cargos + título.
        [[nodiscard]] static float       Get(Id a_faction, Modifier a_modifier);
        [[nodiscard]] static ModifierSet Sum(Id a_faction);  // bônus aditivos crus (debug/UI)

        // Num território: Get() + governador da província do território. Para Production, multiplica também
        // pela mão de obra local (população / população inicial, limitada por Population/*.json).
        [[nodiscard]] static float GetFor(Id a_faction, Id a_territory, Modifier a_modifier);
    };
}
