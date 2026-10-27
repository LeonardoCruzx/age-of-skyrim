#include "Core/State.h"

namespace AoS
{
    State& State::Get() noexcept
    {
        static State instance;
        return instance;
    }

    Id State::Owner(Id a_territory) const
    {
        std::scoped_lock l{ lock_ };
        const auto it = owners_.find(a_territory);
        return it == owners_.end() ? kNeutral : it->second;
    }

    bool State::SetOwner(Id a_territory, Id a_owner)
    {
        std::scoped_lock l{ lock_ };
        auto& slot = owners_[a_territory];
        if (slot == a_owner) {
            return false;
        }
        slot = a_owner;
        return true;
    }

    void State::ResetToDefaults()
    {
        std::scoped_lock l{ lock_ };
        owners_.clear();
        const auto& factions = Factions::Get();
        for (const auto& t : Config::Get().Territories()) {
            if (factions.IsValidOwner(t.defaultOwner)) {
                owners_[t.id] = t.defaultOwner;
            } else {
                logger::warn("'{}': defaultOwner não é uma facção conhecida; usando neutro.", t.key);
                owners_[t.id] = kNeutral;
            }
        }
    }

    // Formato do registro 'TERR' v2:  u32 count, depois count × { u32 territoryId, u32 factionId }.
    // (v1 tinha o mesmo layout, mas o dono era um estilo de bandeira.)
    void State::Save(SKSE::SerializationInterface* a_intfc) const
    {
        std::scoped_lock l{ lock_ };
        if (!a_intfc->OpenRecord(kRecord, kRecordVersion)) {
            logger::error("Cosave: falha ao abrir o registro TERR.");
            return;
        }
        const auto count = static_cast<std::uint32_t>(owners_.size());
        a_intfc->WriteRecordData(count);
        for (const auto& [territory, owner] : owners_) {
            a_intfc->WriteRecordData(territory);
            a_intfc->WriteRecordData(owner);
        }
        logger::info("Cosave: {} território(s) salvo(s).", count);
    }

    void State::LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version)
    {
        if (a_version != 1 && a_version != kRecordVersion) {
            logger::warn("Cosave: versão {} do registro TERR não suportada; usando padrões.", a_version);
            return;
        }
        const auto& factions = Factions::Get();

        std::scoped_lock l{ lock_ };
        std::uint32_t    count{};
        a_intfc->ReadRecordData(count);

        const auto& cfg = Config::Get();
        for (std::uint32_t i = 0; i < count; ++i) {
            Id territory{}, owner{};
            a_intfc->ReadRecordData(territory);
            a_intfc->ReadRecordData(owner);

            if (!cfg.Find(territory)) {
                logger::warn("Cosave: território 0x{:08X} não existe mais na config; descartado.", territory);
                continue;
            }
            if (a_version == 1) {
                owner = owner == kNeutral ? kNeutral : factions.FromLegacyStyle(owner);  // migração
            }
            if (!factions.IsValidOwner(owner)) {
                logger::warn("Cosave: facção 0x{:08X} não existe mais; território volta a neutro.", owner);
                owner = kNeutral;
            }
            owners_[territory] = owner;
        }
        logger::info("Cosave: {} território(s) carregado(s) (TERR v{}).", count, a_version);
    }
}
