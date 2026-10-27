#pragma once

#include "Core/Config.h"

namespace AoS
{
    // Free Claim: estandarte fincado pelo jogador num local genérico (sem território pré-definido).
    // Raycast vertical (bhkWorld::PickObject) à frente do jogador acha o chão e a normal do terreno; o
    // estandarte é criado com Placement::PlaceAt alinhado à normal e sem física. Persistido no registro 'CLAM' v1.
    // TODO(M3/M4): transformar cada claim num território dinâmico (dono = facção do jogador) no Config.
    class Claims
    {
    public:
        struct Claim
        {
            Id           id{ kNeutral };      // estável: hash de "claim/<serial>"
            RE::FormID   worldspace{ 0 };
            RE::NiPoint3 position;
            RE::FormID   ref{ 0 };           // referência do estandarte criada em runtime
        };

        [[nodiscard]] static Claims& Get() noexcept;

        static constexpr std::uint32_t kRecord        = Tag("CLAM");
        static constexpr std::uint32_t kRecordVersion = 1;

        // Ação do jogador (hotkey de debug por enquanto). Só na thread principal. Mostra o resultado na tela.
        void TryClaimHere();

        [[nodiscard]] const std::vector<Claim>& All() const noexcept { return claims_; }

        void Clear();
        void Save(SKSE::SerializationInterface* a_intfc) const;
        void LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version);

    private:
        std::vector<Claim> claims_;
        std::uint32_t      nextSerial_{ 1 };
    };
}
