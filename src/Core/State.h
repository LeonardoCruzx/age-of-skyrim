#pragma once

#include "Core/Config.h"

namespace AoS
{
    // Dono atual de cada território. Persistido no cosave (.skse) — NÃO em script Papyrus.
    class State
    {
    public:
        [[nodiscard]] static State& Get() noexcept;

        [[nodiscard]] Id   Owner(Id a_territory) const;
        bool               SetOwner(Id a_territory, Id a_owner);  // true se mudou
        void               ResetToDefaults();                       // novo jogo / antes de carregar save

        static constexpr std::uint32_t kRecord        = Tag("TERR");
        static constexpr std::uint32_t kRecordVersion = 1;

        void Save(SKSE::SerializationInterface* a_intfc) const;
        void LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version);  // registro já aberto

    private:
        mutable std::mutex              lock_;
        std::unordered_map<Id, Id>      owners_;
    };
}
