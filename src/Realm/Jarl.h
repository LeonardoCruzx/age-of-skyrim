#pragma once

#include "Realm/Data.h"
#include "World/Conquest.h"

namespace AoS::Realm
{
    // M24: presença de um governador nosso na capital de uma província (ADR 0056).
    //  - Capital = território TerritoryType::kCity de maior população (Realm::Population) na província; sem
    //    cidade na província, cai para o território de maior população (heurística documentada na ADR 0056).
    //  - Reaproveita Government::AssignGovernor (M18) para o bônus/maioria; aqui só a PRESENÇA física.
    //  - NPC PRÓPRIO: base vanilla não-única com FaceGen pronto, escolhida em runtime (settings.json -> "jarl",
    //    como Seat faz para baú/mesa) — o jarl vanilla NUNCA é tocado (não editado, não desabilitado; ADR 0005).
    //    Nosso NPC só aparece ao lado, perto do marcador da cidade (World/Placement-style).
    //  - Ativar o NPC mostra um MessageBox só-texto (UI/Prompt.h) com o status da província (população; a
    //    estabilidade é um ponto de extensão para a M26, que ainda não existe).
    //  - Perder a capital (ou a maioria da província) remove o NPC. Registro de cosave PRÓPRIO 'JARL' v1
    //    (província -> FormID do NPC), em vez de estender o GOVT já fechado da M18.
    // Mutações só na thread principal; consultas são thread-safe.
    class Jarl
    {
    public:
        [[nodiscard]] static Jarl& Get() noexcept;

        static constexpr std::uint32_t kRecord        = Tag("JARL");
        static constexpr std::uint32_t kRecordVersion = 1;

        // kDataLoaded, depois de Config/Factions/Conquest/Realm: escolhe o NPC base e liga o listener de
        // conquista e a hotkey de debug (settings.json -> debug.jarlHotkey).
        void Load();

        // ---- consulta ----
        [[nodiscard]] bool       HasNpc(Id a_province) const;
        [[nodiscard]] RE::FormID NpcOf(Id a_province) const;
        [[nodiscard]] Id         CapitalOf(Id a_province) const;

        // ---- ações (thread principal) ----
        // a_territory precisa ser a capital de uma província da facção do jogador. Com settings.jarl.askBeforeAssign
        // pergunta (Prompt::Ask); sem MessageBox disponível, coloca direto (igual ao padrão do Seat).
        void Offer(Id a_territory);
        // Coloca de fato: AssignGovernor (reserva a vaga) + cria o NPC perto do marcador da capital.
        void Place(Id a_faction, Id a_territory);
        // Despawn + remove o registro da província (perder a capital ou a maioria).
        void Remove(Id a_province);
        // Chamado por Realm::Reevaluate com as províncias cujo governador saiu por perda de maioria.
        void OnGovernorsLost(const std::vector<Id>& a_provinces);

        // Debug (hotkey): oferece na capital da província do território atual do jogador.
        void DebugOfferHere();

        void OnPostLoad();
        void Clear();
        void Save(SKSE::SerializationInterface* a_intfc) const;
        void LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version);

        // Ativação do NPC (sink -> thread principal). Não chamar direto.
        void OnActivated(RE::FormID a_npc);

    private:
        struct Entry
        {
            RE::FormID npc{ 0 };
        };

        void ResolveBase();
        void OnConquered(const Territory& a_territory, Id a_previousOwner, Conquest::Source a_source);

        mutable std::mutex                 lock_;
        std::unordered_map<Id, Entry>      byProvince_;      // persistido (JARL)
        std::unordered_map<RE::FormID, Id> npcToProvince_;   // cache: ativação O(1)
        RE::TESBoundObject*                actorBase_{ nullptr };
        RE::BGSOutfit*                      outfitBase_{ nullptr };  // nullptr = outfit padrão do NPC
    };
}
