#include "Realm/Titles.h"

#include "Core/Factions.h"
#include "Realm/Realm.h"

namespace AoS::Realm
{
    namespace
    {
        bool Reached(const TitleRequirements& a_need, const FactionStats& a_stats)
        {
            return a_stats.territories >= a_need.territories &&
                   a_stats.majorityProvinces.size() >= a_need.provinces && a_stats.cities >= a_need.cities &&
                   a_stats.population >= static_cast<double>(a_need.population);
        }
    }

    Titles& Titles::Get() noexcept
    {
        static Titles instance;
        return instance;
    }

    const Title* Titles::Current(Id a_faction) const
    {
        const auto&      data = Data::Get();
        std::scoped_lock l{ Mutex() };
        if (const auto it = titles_.find(a_faction); it != titles_.end()) {
            if (const auto t = data.FindTitle(it->second)) {
                return t;
            }
        }
        return data.Titles().empty() ? nullptr : &data.Titles().front();
    }

    float Titles::Unlock(Id a_faction, std::string_view a_key, float a_fallback) const
    {
        const auto t = Current(a_faction);
        if (!t) {
            return a_fallback;
        }
        const auto it = t->unlocks.find(std::string(a_key));
        return it == t->unlocks.end() ? a_fallback : it->second;
    }

    bool Titles::Meets(Id a_faction, Id a_required) const
    {
        if (a_required == kNeutral) {
            return true;
        }
        const auto t = Current(a_faction);
        return t && t->rank >= Data::Get().RankOf(a_required);
    }

    Titles::Promotion Titles::Evaluate(Id a_faction, const FactionStats& a_stats)
    {
        const auto& list = Data::Get().Titles();
        // Maior título atingido (lista em ordem de rank; poucos títulos -> varredura linear).
        const Title* best = nullptr;
        for (const auto& t : list) {
            if (Reached(t.needs, a_stats)) {
                best = &t;
            }
        }
        if (!best) {
            return {};
        }
        std::scoped_lock l{ Mutex() };
        const auto       it    = titles_.find(a_faction);
        const bool       first = it == titles_.end() || !Data::Get().FindTitle(it->second);
        if (!first && Data::Get().RankOf(it->second) >= best->rank) {
            return {};  // títulos só sobem
        }
        titles_[a_faction] = best->id;
        logger::info("Títulos: '{}' agora é '{}' (rank {}; {} território(s), {} província(s), {} cidade(s), pop. {:.0f}).",
                     Factions::Get().NameOf(a_faction), best->key, best->rank, a_stats.territories,
                     a_stats.majorityProvinces.size(), a_stats.cities, a_stats.population);
        return { best, first };
    }

    void Titles::AddModifiers(Id a_faction, ModifierSet& a_io) const
    {
        if (const auto t = Current(a_faction)) {
            for (std::size_t i = 0; i < kModifierCount; ++i) {
                a_io[i] += t->modifiers[i];
            }
        }
    }

    void Titles::Clear()
    {
        std::scoped_lock l{ Mutex() };
        titles_.clear();
    }

    // Formato do registro 'TITL' v1:  u32 count, depois count × { u32 faction, u32 title }.
    void Titles::Save(SKSE::SerializationInterface* a_intfc) const
    {
        std::scoped_lock l{ Mutex() };
        if (titles_.empty()) {
            return;
        }
        if (!a_intfc->OpenRecord(kRecord, kRecordVersion)) {
            logger::error("Cosave: falha ao abrir o registro TITL.");
            return;
        }
        a_intfc->WriteRecordData(static_cast<std::uint32_t>(titles_.size()));
        for (const auto& [faction, title] : titles_) {
            a_intfc->WriteRecordData(faction);
            a_intfc->WriteRecordData(title);
        }
        logger::info("Cosave: TITL v{} salvo ({} título(s), {} bytes).", kRecordVersion, titles_.size(),
                     4 + titles_.size() * 8);
    }

    void Titles::LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version)
    {
        if (a_version != kRecordVersion) {
            logger::warn("Cosave: versão {} do registro TITL não suportada; títulos serão reavaliados.", a_version);
            return;
        }
        std::scoped_lock l{ Mutex() };
        std::uint32_t    count{};
        a_intfc->ReadRecordData(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            Id faction{}, title{};
            a_intfc->ReadRecordData(faction);
            a_intfc->ReadRecordData(title);
            if (!Factions::Get().Find(faction) || !Data::Get().FindTitle(title)) {
                logger::warn("Cosave: título 0x{:08X} da facção 0x{:08X} não existe mais; será reavaliado.", title, faction);
                continue;
            }
            titles_[faction] = title;
        }
        logger::info("Cosave: TITL v{} carregado ({} título(s)).", a_version, titles_.size());
    }
}
