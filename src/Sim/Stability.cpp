#include "Sim/Stability.h"

#include "Banner/BannerController.h"
#include "Core/Factions.h"
#include "Core/I18n.h"
#include "Core/State.h"
#include "Core/Timer.h"
#include "Realm/Modifiers.h"
#include "Realm/Population.h"
#include "Sim/Economy.h"
#include "Sim/Garrison.h"
#include "World/Conquest.h"
#include "World/Seat.h"

namespace AoS
{
    namespace
    {
        // Mesma técnica de Ai::Unit (splitmix64): pseudoaleatório em [0,1), reproduzível no log, sem estado
        // próprio. Duplicado aqui de propósito (utilitário pequeno, não vale a pena um Core novo só para isto).
        [[nodiscard]] double Unit(std::uint64_t a_seed)
        {
            std::uint64_t z = a_seed + 0x9E3779B97F4A7C15ull;
            z               = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z               = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            z ^= z >> 31;
            return static_cast<double>(z >> 11) * (1.0 / 9007199254740992.0);
        }

        [[nodiscard]] double HoursNow()
        {
            const auto cal = RE::Calendar::GetSingleton();
            return cal ? static_cast<double>(cal->GetHoursPassed()) : 0.0;
        }

        bool IsPlayerFaction(Id a_faction)
        {
            const auto f = Factions::Get().Find(a_faction);
            return f && f->player;
        }
    }

    Stability& Stability::Get() noexcept
    {
        static Stability instance;
        return instance;
    }

    void Stability::Load()
    {
        ScopedTimer timer{ "Stability::Load" };
        rebelsFaction_ = kNeutral;
        if (const auto f = Factions::Get().Find(MakeId("rebels"))) {
            rebelsFaction_ = f->id;
        } else {
            // Ausente = aviso (nunca crash); a rebelião cai para kNeutral mesmo com stability.rebelFaction=true.
            // Ver data/SKSE/Plugins/AgeOfSkyrim/Factions/rebels.json (facção fixa, não-jogador, ADR 0063).
            logger::warn("Estabilidade: facção 'rebels' não encontrada (Factions/rebels.json ausente ou estilo "
                         "'rebels' não declarado); rebeliões viram território neutro.");
        }
        const auto& cfg = Config::Get().Stability();
        logger::info("Estabilidade: {} (base {:.0f}, limiar {:.0f}, graça {} h, {:.1f}%/h depois da graça, "
                     "rebelião -> {}).",
                     cfg.enabled ? "ligada" : "desligada", cfg.baseline, cfg.rebellionThreshold, cfg.graceHours,
                     cfg.chancePerHour * 100.0, cfg.rebelFaction ? "facção 'rebels'" : "neutro");

        Economy::Get().AddTickListener("stability", [](std::uint32_t a_hours) { Stability::Get().OnHours(a_hours); });
    }

    // ---- consulta -------------------------------------------------------------------------------------

    double Stability::Score(Id a_territory) const
    {
        std::scoped_lock l{ lock_ };
        const auto       it = scores_.find(a_territory);
        return it == scores_.end() ? Config::Get().Stability().baseline : it->second;
    }

    std::uint32_t Stability::UnstableHoursOf(Id a_territory) const
    {
        std::scoped_lock l{ lock_ };
        const auto       it = unstableHours_.find(a_territory);
        return it == unstableHours_.end() ? 0 : it->second;
    }

    void Stability::ReportUnrest(Id a_faction, double a_amount)
    {
        if (a_faction == kNeutral || a_amount <= 0.0) {
            return;
        }
        std::scoped_lock l{ lock_ };
        unrest_[a_faction] += a_amount;
    }

    // ---- cálculo --------------------------------------------------------------------------------------

    // lock_ NÃO travado aqui: só lê outros módulos (cada um com o próprio lock) e o cache local de unrest_,
    // protegido por um lock curto dentro desta função (nunca aninhado com o lock de outro módulo).
    double Stability::ComputeScore(const Territory& a_territory, Id a_owner) const
    {
        const auto& cfg = Config::Get().Stability();
        double      score =
            cfg.baseline * Realm::Modifiers::GetFor(a_owner, a_territory.id, Realm::Modifier::Stability);

        // Guarnição insuficiente para a população: quanto maior o território, mais força de ordem ele pede.
        const auto population = static_cast<double>(Realm::Population::Get().Of(a_territory.id));
        const auto required   = population * cfg.popToStrength;
        if (required > 0.0) {
            const auto have = Garrison::Get().Strength(a_territory.id);
            if (have < required) {
                score -= (1.0 - have / required) * cfg.garrisonPenalty;
            }
        }
        if (Economy::Get().InDeficit(a_owner)) {
            score -= cfg.deficitPenalty;
        }
        {
            std::scoped_lock l{ lock_ };
            if (const auto it = unrest_.find(a_owner); it != unrest_.end()) {
                score -= it->second;
            }
        }
        return std::clamp(score, 0.0, 100.0);
    }

    void Stability::CheckRebellion(const Territory& a_territory, Id a_owner, std::uint32_t a_hours, bool a_forceRoll)
    {
        const auto& cfg = Config::Get().Stability();
        // Chance de NÃO rebelar em nenhuma das a_hours horas: (1 - chancePerHour)^horas.
        const auto pNoRebel = std::pow(1.0 - cfg.chancePerHour, static_cast<double>(std::max(1u, a_hours)));
        const auto roll     = Unit((static_cast<std::uint64_t>(a_territory.id) << 32) ^
                               static_cast<std::uint64_t>(HoursNow() * 10.0));
        if (!a_forceRoll && roll < pNoRebel) {
            return;  // não rebelou desta vez
        }
        if (a_forceRoll && roll < pNoRebel) {
            I18n::NotifyTr("stability.debugNoRebellion");
            return;
        }
        const auto target = cfg.rebelFaction && rebelsFaction_ != kNeutral ? rebelsFaction_ : kNeutral;
        if (!SetTerritoryOwner(a_territory.id, target)) {
            return;
        }
        {
            std::scoped_lock l{ lock_ };
            unstableHours_.erase(a_territory.id);
            warned_.erase(a_territory.id);
        }
        logger::info("Estabilidade: '{}' se rebelou contra '{}' -> {}.", a_territory.key,
                     Factions::Get().NameOf(a_owner), Factions::Get().NameOf(target));
        if (IsPlayerFaction(a_owner)) {
            I18n::NotifyTr("stability.rebelled", a_territory.name);
        }
    }

    void Stability::OnHours(std::uint32_t a_hours)
    {
        const auto& cfg = Config::Get().Stability();
        if (!cfg.enabled || a_hours == 0) {
            return;
        }
        ScopedTimer timer{ "Stability::OnHours" };
        const auto& state = State::Get();

        std::vector<std::pair<Id, Id>> toCheck;  // (território, dono) elegíveis para o roll de rebelião

        {
            std::scoped_lock l{ lock_ };
            // Abalo de execuções (Law::Execute) decai por hora, igual déficit/deserção de outros módulos.
            for (auto it = unrest_.begin(); it != unrest_.end();) {
                it->second -= cfg.unrestDecayPerHour * a_hours;
                if (it->second <= 0.0) {
                    it = unrest_.erase(it);
                } else {
                    ++it;
                }
            }
        }

        Config::Get().ForEachTerritory([&](const Territory& a_t) {
            const auto owner = state.Owner(a_t.id);
            if (owner == kNeutral) {
                std::scoped_lock l{ lock_ };
                unstableHours_.erase(a_t.id);
                warned_.erase(a_t.id);
                scores_.erase(a_t.id);
                return;
            }
            const auto score = ComputeScore(a_t, owner);
            bool       crossed = false;
            bool       eligible = false;
            {
                std::scoped_lock l{ lock_ };
                scores_[a_t.id] = score;
                if (score < cfg.rebellionThreshold) {
                    auto& h = unstableHours_[a_t.id];
                    h += a_hours;
                    if (!warned_.contains(a_t.id)) {
                        warned_.insert(a_t.id);
                        crossed = true;
                    }
                    eligible = h >= cfg.graceHours;
                } else {
                    unstableHours_.erase(a_t.id);
                    warned_.erase(a_t.id);
                }
            }
            if (crossed && IsPlayerFaction(owner)) {
                I18n::NotifyTr("stability.unstable", a_t.name);
            }
            if (eligible) {
                toCheck.emplace_back(a_t.id, owner);
            }
        });

        for (const auto& [id, owner] : toCheck) {
            if (const auto t = Config::Get().Find(id)) {
                CheckRebellion(*t, owner, a_hours, /*forceRoll*/ false);
            }
        }
    }

    // ---- Debug ----------------------------------------------------------------------------------------

    void Stability::DebugHere()
    {
        auto t = Conquest::Get().TerritoryAtPlayer(/*onlyCleared*/ false);
        if (!t) {
            if (const auto seat = Seat::Get().SeatTerritory(); seat != kNeutral) {
                t = Config::Get().Find(seat);
            } else {
                t = Config::Get().Find(Config::Get().Debug().territory);
            }
        }
        if (!t) {
            I18n::NotifyTr("stability.noTerritory");
            return;
        }
        const auto owner = State::Get().Owner(t->id);
        if (owner == kNeutral) {
            I18n::NotifyTr("stability.neutral", t->name);
            return;
        }
        const auto score  = Score(t->id);
        const auto hours  = UnstableHoursOf(t->id);
        const auto& cfg   = Config::Get().Stability();
        I18n::NotifyTr("stability.status", t->name, Factions::Get().NameOf(owner), std::format("{:.0f}", score),
                       std::format("{:.0f}", cfg.rebellionThreshold), hours, cfg.graceHours);
        CheckRebellion(*t, owner, /*a_hours*/ 1, /*forceRoll*/ true);
    }

    void Stability::Clear()
    {
        std::scoped_lock l{ lock_ };
        scores_.clear();
        unstableHours_.clear();
        warned_.clear();
        unrest_.clear();
    }
}
