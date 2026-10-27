#pragma once

#include "Stock/Stock.h"
#include "World/Conquest.h"

#include <atomic>

namespace AoS
{
    // M6: sede (capital) da facção do jogador (ADR 0011 / 0023).
    //  - Candidatas: Data/SKSE/Plugins/AgeOfSkyrim/Seats/*.json (lista explícita + regra automática por província);
    //    outros mods adicionam arquivos na pasta.
    //  - Ao conquistar uma candidata, pergunta "Fundar sede aqui?" (ou "Mudar a sede?") com MessageBox vanilla.
    //  - Baú de estoque e mesa de guerra criados em runtime (Placement::PlaceAt), FormIDs no registro 'SEAT' v1.
    //  - Mudar de sede move os itens do baú antigo para o novo e apaga os objetos antigos.
    //  - O baú é observado pelo Stock (recontado ao fechar); a mesa de guerra tem um gancho de ativação (M11).
    // Mutações só na thread principal; getters são thread-safe (M9: API para outros mods).
    class Seat
    {
    public:
        struct Candidate
        {
            Id                          territory{ kNeutral };
            std::optional<RE::NiPoint3> chestOffset;     // sobrescreve settings.seat.chestOffset
            std::optional<RE::NiPoint3> warTableOffset;  // sobrescreve settings.seat.warTableOffset
        };

        [[nodiscard]] static Seat& Get() noexcept;

        static constexpr std::uint32_t kRecord        = Tag("SEAT");
        static constexpr std::uint32_t kRecordVersion = 1;

        // kDataLoaded, depois de Config/Factions/Stock: lê Seats/*.json, escolhe os objetos base, registra o
        // listener de conquista e o sink de ativação da mesa de guerra.
        void Load();

        // ---- consulta (M9/M10/M11/M12) ----
        [[nodiscard]] bool                         IsCandidate(Id a_territory) const;
        [[nodiscard]] std::vector<Id>              Candidates() const;
        [[nodiscard]] bool                         HasSeat() const;
        [[nodiscard]] Id                           SeatTerritory() const;  // kNeutral se não tem sede
        [[nodiscard]] RE::TESObjectREFR*           Chest() const;          // nullptr se não tem / sumiu
        [[nodiscard]] RE::TESObjectREFR*           WarTable() const;
        [[nodiscard]] RE::FormID                   ChestId() const;
        [[nodiscard]] std::optional<Stock::Totals> StockTotals() const;    // última contagem do baú (por categoria)

        // ---- ações (thread principal) ----
        // Funda (ou muda) a sede no território, que precisa ser da facção do jogador. Objetos nascem relativos ao
        // jogador (ainda sem coordenadas curadas). M12 deposita itens com Chest()->AddObjectToContainer.
        bool Found(Id a_territory);
        // Pergunta ao jogador se quer fundar/mudar a sede (fallback sem MessageBox: funda direto se não tem sede).
        void Offer(const Territory& a_territory);
        // M11: abrir o mapa tático. Por enquanto só notifica e loga.
        void OnWarTableActivated();
        // Debug (F8): estoque da sede na tela + log.
        void ReportStock();

        // Depois de carregar o save / jogo novo: observa e reconta o baú.
        void OnPostLoad();
        void Clear();
        void Save(SKSE::SerializationInterface* a_intfc) const;
        void LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version);

        [[nodiscard]] RE::FormID WarTableId() const noexcept { return warTableId_.load(std::memory_order_relaxed); }

    private:
        void LoadFile(const fs::path& a_file);
        void ResolveBases();
        void OnConquered(const Territory& a_territory, Id a_previousOwner, Conquest::Source a_source);

        mutable std::mutex                lock_;
        std::unordered_map<Id, Candidate> candidates_;
        Id                                territory_{ kNeutral };
        RE::FormID                        chest_{ 0 };
        RE::FormID                        warTable_{ 0 };
        std::atomic<RE::FormID>           warTableId_{ 0 };  // cópia sem lock para o sink de ativação (caminho quente)

        RE::TESBoundObject* chestBase_{ nullptr };
        RE::TESBoundObject* warTableBase_{ nullptr };
    };
}
