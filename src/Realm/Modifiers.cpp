#include "Realm/Modifiers.h"

#include "Core/Config.h"
#include "Realm/Government.h"
#include "Realm/Population.h"
#include "Realm/Titles.h"

namespace AoS::Realm
{
    namespace
    {
        constexpr std::array<std::string_view, kModifierCount> kNames{
            "production", "tax", "trade", "recruitCost", "recruitSpeed", "populationGrowth", "defense", "upkeep",
        };

        float Multiplier(float a_sum) noexcept { return std::max(kMinMultiplier, 1.0f + a_sum); }
    }

    std::string_view ToString(Modifier a_modifier) noexcept
    {
        const auto i = static_cast<std::size_t>(a_modifier);
        return i < kModifierCount ? kNames[i] : "?";
    }

    std::optional<Modifier> ParseModifier(std::string_view a_text) noexcept
    {
        for (std::size_t i = 0; i < kModifierCount; ++i) {
            if (a_text.size() == kNames[i].size() && _strnicmp(a_text.data(), kNames[i].data(), a_text.size()) == 0) {
                return static_cast<Modifier>(i);
            }
        }
        return std::nullopt;
    }

    void ParseModifiers(const nlohmann::json& a_json, ModifierSet& a_out, std::string_view a_context)
    {
        if (!a_json.is_object()) {
            logger::warn("'{}': \"modifiers\" precisa ser um objeto {{ \"production\": 0.1, ... }}.", a_context);
            return;
        }
        for (const auto& [key, value] : a_json.items()) {
            const auto m = ParseModifier(key);
            if (!m || !value.is_number()) {
                logger::warn("'{}': modificador '{}' desconhecido ou não numérico; ignorado.", a_context, key);
                continue;
            }
            a_out[static_cast<std::size_t>(*m)] += value.get<float>();
        }
    }

    ModifierSet Modifiers::Sum(Id a_faction)
    {
        ModifierSet sum{};
        if (a_faction == kNeutral) {
            return sum;
        }
        Government::Get().AddModifiers(a_faction, sum);
        Titles::Get().AddModifiers(a_faction, sum);
        return sum;
    }

    float Modifiers::Get(Id a_faction, Modifier a_modifier)
    {
        if (a_faction == kNeutral || a_modifier >= Modifier::kCount) {
            return 1.0f;
        }
        return Multiplier(Sum(a_faction)[static_cast<std::size_t>(a_modifier)]);
    }

    float Modifiers::GetFor(Id a_faction, Id a_territory, Modifier a_modifier)
    {
        if (a_modifier >= Modifier::kCount) {
            return 1.0f;
        }
        const auto i   = static_cast<std::size_t>(a_modifier);
        float      sum = a_faction == kNeutral ? 0.0f : Sum(a_faction)[i];
        if (const auto t = Config::Get().Find(a_territory); t && a_faction != kNeutral &&
                                                             Government::Get().HasGovernor(a_faction, t->province)) {
            sum += Data::Get().Governor().modifiers[i];
        }
        auto result = Multiplier(sum);
        if (a_modifier == Modifier::Production) {
            result *= Population::Get().Manpower(a_territory);  // mão de obra local
        }
        return result;
    }
}
