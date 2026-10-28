#include "Realm/Domination.h"

#include "Core/Factions.h"
#include "Realm/Realm.h"

namespace AoS::Realm
{
    Domination& Domination::Get() noexcept
    {
        static Domination instance;
        return instance;
    }

    bool Domination::Of(Id a_faction) const
    {
        std::scoped_lock l{ lock_ };
        return dominated_.contains(a_faction);
    }

    bool Domination::Evaluate(Id a_faction, const FactionStats& a_stats)
    {
        const auto total = Data::Get().ProvinceCount();
        if (total == 0 || a_faction == kNeutral) {
            return false;
        }
        std::scoped_lock l{ lock_ };
        if (dominated_.contains(a_faction)) {
            return false;  // marco permanente: já tinha
        }
        const auto threshold = std::clamp(Config::Get().Realm().dominationThreshold, 0.0f, 1.0f);
        const auto fraction  = static_cast<float>(a_stats.majorityProvinces.size()) / static_cast<float>(total);
        if (fraction + 1e-4f < threshold) {
            return false;
        }
        dominated_.insert(a_faction);
        logger::info("Domínio: '{}' controla {}/{} província(s) (>= {:.0f}%) — Dominador(a) de Skyrim.",
                     Factions::Get().NameOf(a_faction), a_stats.majorityProvinces.size(), total, threshold * 100.0f);
        return true;
    }

    void Domination::Clear()
    {
        std::scoped_lock l{ lock_ };
        dominated_.clear();
    }

    // Formato do registro 'DOMN' v1:  u32 count, depois count × Id facção.
    void Domination::Save(SKSE::SerializationInterface* a_intfc) const
    {
        std::scoped_lock l{ lock_ };
        if (dominated_.empty()) {
            return;
        }
        if (!a_intfc->OpenRecord(kRecord, kRecordVersion)) {
            logger::error("Cosave: falha ao abrir o registro DOMN.");
            return;
        }
        a_intfc->WriteRecordData(static_cast<std::uint32_t>(dominated_.size()));
        for (const auto f : dominated_) {
            a_intfc->WriteRecordData(f);
        }
        logger::info("Cosave: DOMN v{} salvo ({} facção(ões) dominadora(s)).", kRecordVersion, dominated_.size());
    }

    void Domination::LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version)
    {
        if (a_version != kRecordVersion) {
            logger::warn("Cosave: versão {} do registro DOMN não suportada; domínio será reavaliado.", a_version);
            return;
        }
        std::uint32_t count{};
        a_intfc->ReadRecordData(count);
        std::scoped_lock l{ lock_ };
        for (std::uint32_t i = 0; i < count; ++i) {
            Id faction{};
            a_intfc->ReadRecordData(faction);
            if (!Factions::Get().Find(faction)) {
                logger::warn("Cosave: domínio da facção 0x{:08X} que não existe mais; descartado.", faction);
                continue;
            }
            dominated_.insert(faction);
        }
        logger::info("Cosave: DOMN v{} carregado ({} facção(ões) dominadora(s)).", a_version, dominated_.size());
    }
}
