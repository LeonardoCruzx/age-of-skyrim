#pragma once

#include "Core/Config.h"

namespace AoS
{
    // Free Claim: estandarte fincado pelo jogador num local genérico (sem território pré-definido).
    // Raycast vertical (bhkWorld::PickObject) à frente do jogador acha o chão e a normal do terreno; o
    // estandarte é criado com Placement::PlaceAt alinhado à normal e sem física. Persistido no registro 'CLAM' v2.
    // Cada claim é um território dinâmico (Config::AddDynamic, tipo "claim") da facção que o fincou (ADR 0024):
    // aparece em Config::Find/ForEachTerritory e o dono fica no TERR como qualquer território.
    class Claims
    {
    public:
        struct Claim
        {
            Id           id{ kNeutral };      // estável: hash de "claim/<serial>"
            RE::FormID   worldspace{ 0 };
            RE::NiPoint3 position;
            RE::FormID   ref{ 0 };           // referência do estandarte criada em runtime
            std::uint32_t serial{ 0 };       // número do claim (nome exibido)
            Id           founder{ kNeutral }; // facção que fincou (dono inicial do território)
        };

        [[nodiscard]] static Claims& Get() noexcept;

        static constexpr std::uint32_t kRecord        = Tag("CLAM");
        static constexpr std::uint32_t kRecordVersion = 2;  // v2: + serial e facção (v1 migra: facção do jogador)

        // Ação do jogador (hotkey de debug por enquanto). Só na thread principal. Mostra o resultado na tela.
        void TryClaimHere();

        [[nodiscard]] const std::vector<Claim>& All() const noexcept { return claims_; }

        void Clear();
        void Save(SKSE::SerializationInterface* a_intfc) const;
        void LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version);

    private:
        // Registra o claim como território dinâmico e define o dono inicial (sem mexer no mundo).
        static void RegisterTerritory(const Claim& a_claim);

        std::vector<Claim> claims_;
        std::uint32_t      nextSerial_{ 1 };
    };
}
