#pragma once

#include "Core/Config.h"

namespace AoS
{
    // M25: lei e prisioneiros de guerra (ADR 0060/0061). Tudo numérico, sem atores (prisioneiros são só uma
    // contagem por facção, 'Law::Prisoners'), consistente com o resto do reino (Garrison/Upgrades/Realm).
    //  - Facção de crime vanilla do Hold (bounty): ao trocar o dono de um território (gancho em
    //    BannerController::SetTerritoryOwner, ao lado de Realm::OnTerritoryOwnerChanged), se
    //    settings.json -> law.touchCrimeFaction e law.resetBounty estiverem ligados, zera a recompensa do
    //    jogador na facção de crime do Hold (RE::BGSLocation::unreportedCrimeFaction, campo FNAM do Hold —
    //    é literalmente a mesma facção usada pelo jogo para o bounty daquele Hold; não é um FormID de
    //    memória, é um campo documentado do CommonLibSSE-NG resolvido em runtime). Default DESLIGADO
    //    (ADR 0005): zero interferência com mods de Guerra Civil/lei a menos que o usuário ligue.
    //  - Prisioneiros: ganhos por combates resolvidos pela IA (M16, Ai::Resolve chama CaptureFromCombat) ou
    //    por um hotkey de debug que simula capturas. Uma fração (law.captureFraction) dos soldados
    //    derrotados vira prisioneiro do vencedor, até a capacidade da facção (Capacity): base por território
    //    (law.baseCapacityPerTerritory) + melhoria "prison" (Upgrades::Stat kPrisonCapacity). O excedente é
    //    só baixa (sem efeito numérico extra). Ações sobre os prisioneiros que uma facção já detém:
    //      Ransom (resgate): custa ouro (law.ransomCostPerPrisoner cada); jogador paga com itens reais do
    //        baú da sede (como Upgrades::Start); IA numericamente via Economy::Spend.
    //      Execute (execução): efeito leve e imediato na estabilidade (M26, Stability::ReportUnrest) da
    //        própria facção que executa (brutalidade interna custa ordem pública) — log sempre, efeito
    //        best-effort (não quebra se Stability não estiver carregada).
    // Thread-safe (mutex próprio); ações e consultas podem vir de qualquer thread (debug hotkeys já chegam
    // pela fila da thread principal, como o resto do mod).
    class Law
    {
    public:
        enum class ActionResult : std::uint8_t
        {
            kOk,
            kNone,            // a facção não tem prisioneiros
            kInsufficientGold,
            kNoSeat,          // facção do jogador sem sede (sem baú para pagar o resgate)
        };

        [[nodiscard]] static Law& Get() noexcept;

        static constexpr std::uint32_t kRecord        = Tag("LAWS");
        static constexpr std::uint32_t kRecordVersion = 1;

        // kDataLoaded, depois de Config/Factions/Economy/Stock/Seat/Upgrades (capacidade e pagamento do resgate).
        void Load();

        // ---- prisioneiros ----
        [[nodiscard]] std::uint32_t Prisoners(Id a_faction) const;
        // Teto: baseCapacityPerTerritory por território da facção + Upgrades::Stat(kPrisonCapacity) de cada um.
        [[nodiscard]] std::uint32_t Capacity(Id a_faction) const;

        // ---- integração ----
        // Ponto de integração com combates resolvidos (M16, Ai::Resolve chama isto quando um ataque termina em
        // captura; o debug hotkey de captura simulada também usa isto). a_fromFaction só entra no log/notificação
        // (prisioneiros são agregados por facção, sem origem individual). Resultado é limitado pela capacidade do
        // captor; o resto é descartado (baixas), sem efeito numérico adicional.
        void CaptureFromCombat(Id a_capturingFaction, Id a_fromFaction, std::uint32_t a_defeatedCount);

        // ---- ações (jogador / API futura) ----
        ActionResult Ransom(Id a_faction, std::uint32_t a_count = 1);
        ActionResult Execute(Id a_faction, std::uint32_t a_count = 1);

        // Gancho de troca de dono (chamado por BannerController::SetTerritoryOwner, ao lado de
        // Realm::OnTerritoryOwnerChanged). Só age se law.touchCrimeFaction && law.resetBounty.
        void OnTerritoryOwnerChanged(Id a_territory, Id a_oldOwner, Id a_newOwner);

        // ---- Debug (hotkeys) ----
        void DebugCapture();      // simula law.debugCaptureCount capturas no território atual
        void DebugRansomFirst();  // resgata 1 prisioneiro da facção do jogador
        void DebugExecuteFirst(); // executa 1 prisioneiro da facção do jogador

        void Clear();
        void Save(SKSE::SerializationInterface* a_intfc) const;
        void LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version);

    private:
        [[nodiscard]] std::uint32_t CapacityUnlocked(Id a_faction) const;  // lock_ travado

        mutable std::mutex                    lock_;
        std::unordered_map<Id, std::uint32_t> prisoners_;  // facção -> contagem (cosave)
    };
}
