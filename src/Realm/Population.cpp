#include "Realm/Population.h"

#include "Core/State.h"
#include "Core/Timer.h"
#include "Realm/Modifiers.h"
#include "Realm/Realm.h"

namespace AoS::Realm
{
    Population& Population::Get() noexcept
    {
        static Population instance;
        return instance;
    }

    void Population::SetFoodProvider(FoodProvider a_provider)
    {
        std::scoped_lock l{ Mutex() };
        food_ = std::move(a_provider);
        logger::info("População: gancho de comida {}.", food_ ? "registrado" : "removido");
    }

    void Population::Reset()
    {
        const auto&      list = Config::Get().Territories();
        const auto&      data = Data::Get();
        std::scoped_lock l{ Mutex() };
        initial_.resize(list.size());
        for (std::size_t i = 0; i < list.size(); ++i) {
            initial_[i] = data.InitialPopulation(list[i]);
        }
        current_ = initial_;
    }

    float Population::Of(Id a_territory) const
    {
        const auto       i = Config::Get().IndexOf(a_territory);
        std::scoped_lock l{ Mutex() };
        return i && *i < current_.size() ? current_[*i] : 0.0f;
    }

    float Population::Initial(Id a_territory) const
    {
        const auto       i = Config::Get().IndexOf(a_territory);
        std::scoped_lock l{ Mutex() };
        return i && *i < initial_.size() ? initial_[*i] : 0.0f;
    }

    float Population::AtIndex(std::size_t a_index) const
    {
        std::scoped_lock l{ Mutex() };
        return a_index < current_.size() ? current_[a_index] : 0.0f;
    }

    double Population::Total(Id a_faction) const
    {
        const auto&      list  = Config::Get().Territories();
        double           total = 0.0;
        std::scoped_lock l{ Mutex() };
        for (std::size_t i = 0; i < list.size() && i < current_.size(); ++i) {
            if (State::Get().Owner(list[i].id) == a_faction) {
                total += current_[i];
            }
        }
        return total;
    }

    float Population::Manpower(Id a_territory) const
    {
        const auto&      def = Data::Get().Population();
        const auto       i   = Config::Get().IndexOf(a_territory);
        std::scoped_lock l{ Mutex() };
        if (!i || *i >= current_.size() || initial_[*i] <= 0.0f) {
            return 1.0f;
        }
        return std::clamp(current_[*i] / initial_[*i], def.manpowerMin, def.manpowerMax);
    }

    std::uint32_t Population::Draft(Id a_territory, std::uint32_t a_count)
    {
        const auto&      def = Data::Get().Population();
        const auto       i   = Config::Get().IndexOf(a_territory);
        std::scoped_lock l{ Mutex() };
        if (!i || *i >= current_.size()) {
            return 0;
        }
        const auto reserve   = initial_[*i] * def.draftReserve;
        const auto available = std::max(0.0f, std::floor(current_[*i] - reserve));
        const auto drafted   = static_cast<std::uint32_t>(std::min(available, static_cast<float>(a_count)));
        current_[*i] -= static_cast<float>(drafted);
        logger::info("População: {} alistado(s) em '{}' (restam {:.0f}).", drafted, Config::Get().NameOf(a_territory),
                     current_[*i]);
        return drafted;
    }

    void Population::OnCaptured(Id a_territory)
    {
        const auto&      def = Data::Get().Population();
        const auto       i   = Config::Get().IndexOf(a_territory);
        std::scoped_lock l{ Mutex() };
        if (i && *i < current_.size()) {
            current_[*i] = std::max(1.0f, current_[*i] * (1.0f - def.captureLoss));
        }
    }

    void Population::Tick(float a_hours)
    {
        if (a_hours <= 0.0f) {
            return;
        }
        const auto& list = Config::Get().Territories();
        const auto& def  = Data::Get().Population();
        const auto  days = a_hours / 24.0f;

        // Fora do mutex: dono de cada território e, por facção dona, comida (gancho da economia) + crescimento.
        struct OwnerInfo
        {
            Id    owner;
            bool  fed;
            float growth;
        };
        std::vector<Id>        owners(list.size());
        std::vector<OwnerInfo> infos;  // poucas facções -> busca linear
        FoodProvider           food;
        {
            std::scoped_lock l{ Mutex() };
            food = food_;
        }
        for (std::size_t i = 0; i < list.size(); ++i) {
            const auto o = State::Get().Owner(list[i].id);
            owners[i]    = o;
            if (std::ranges::none_of(infos, [o](const OwnerInfo& a) { return a.owner == o; })) {
                const bool fed = o == kNeutral || !food || food(o);  // neutro: vila sem dono se sustenta
                infos.push_back({ o, fed, o == kNeutral ? 1.0f : Modifiers::Get(o, Modifier::PopulationGrowth) });
            }
        }

        std::scoped_lock l{ Mutex() };
        const auto       n = std::min(list.size(), current_.size());
        for (std::size_t i = 0; i < n; ++i) {
            const auto& info = *std::ranges::find_if(infos, [&](const OwnerInfo& a) { return a.owner == owners[i]; });
            auto&       p    = current_[i];
            const auto  cap  = initial_[i] * def.capacityFactor;
            if (info.fed) {
                p += def.growthPerDay * info.growth * p * (1.0f - p / cap) * days;  // logístico até o teto
            } else {
                p -= def.starvationPerDay * p * days;
            }
            p = std::clamp(p, 1.0f, cap);
        }
    }

    // Formato do registro 'POPL' v1 (compacto: só territórios cuja população arredondada difere da inicial):
    //   u32 count, depois count × { u32 territoryId, u16 population }.
    void Population::Save(SKSE::SerializationInterface* a_intfc) const
    {
        const auto&      list = Config::Get().Territories();
        std::scoped_lock l{ Mutex() };
        const auto       n = std::min(list.size(), current_.size());

        const auto rounded = [](float a_v) {
            return static_cast<std::uint16_t>(std::clamp(std::lround(a_v), 0l, 0xFFFFl));
        };
        std::uint32_t count = 0;
        for (std::size_t i = 0; i < n; ++i) {
            count += rounded(current_[i]) != rounded(initial_[i]) ? 1u : 0u;
        }
        if (count == 0) {
            return;
        }
        if (!a_intfc->OpenRecord(kRecord, kRecordVersion)) {
            logger::error("Cosave: falha ao abrir o registro POPL.");
            return;
        }
        a_intfc->WriteRecordData(count);
        for (std::size_t i = 0; i < n; ++i) {
            const auto v = rounded(current_[i]);
            if (v != rounded(initial_[i])) {
                a_intfc->WriteRecordData(list[i].id);
                a_intfc->WriteRecordData(v);
            }
        }
        logger::info("Cosave: POPL v{} salvo ({} de {} território(s) fora do padrão, {} bytes).", kRecordVersion, count, n,
                     4 + count * 6u);
    }

    void Population::LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version)
    {
        if (a_version != kRecordVersion) {
            logger::warn("Cosave: versão {} do registro POPL não suportada; população inicial.", a_version);
            return;
        }
        const auto&      cfg = Config::Get();
        std::scoped_lock l{ Mutex() };
        std::uint32_t    count{}, applied{ 0 };
        a_intfc->ReadRecordData(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            Id            territory{};
            std::uint16_t value{};
            a_intfc->ReadRecordData(territory);
            a_intfc->ReadRecordData(value);
            if (const auto idx = cfg.IndexOf(territory); idx && *idx < current_.size()) {
                current_[*idx] = std::max<float>(1.0f, value);
                ++applied;
            }
        }
        logger::info("Cosave: POPL v{} carregado ({} de {} território(s) aplicados).", a_version, applied, count);
    }
}
