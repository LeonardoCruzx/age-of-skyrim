#pragma once

#include "Core/Config.h"

namespace AoS
{
    // Objetos que o DLL cria no mundo em runtime (bandeiras de teste; depois baú/mesa da sede).
    // Guarda PlacedSpec::key -> FormID da referência criada no cosave (registro 'PLAC' v1).
    // Criar no runtime em vez de pré-colocar no ESP evita editar células vanilla (compatibilidade).
    class Placement
    {
    public:
        [[nodiscard]] static Placement& Get() noexcept;

        static constexpr std::uint32_t kRecord        = Tag("PLAC");
        static constexpr std::uint32_t kRecordVersion = 1;

        // nullptr se ainda não foi criado neste save (ou se a referência sumiu).
        [[nodiscard]] RE::TESObjectREFR* Find(Id a_key) const;

        // Cria, relativo ao jogador, os objetos do território que ainda não existem. Só na thread principal.
        // Retorna quantos falharam (base inexistente, jogador sem célula carregada...).
        std::size_t EnsurePlaced(const Territory& a_territory);

        // Cria um objeto em posição/ângulo absolutos na célula do jogador (não entra no registro PLAC: quem
        // chama guarda o FormID, ex.: Claims). a_keyframed: desliga a física (itens não caem nem tombam).
        // a_origin (M15): cria na célula desta referência (precisa estar com a célula carregada) em vez da do jogador.
        [[nodiscard]] static RE::TESObjectREFR* PlaceAt(RE::TESBoundObject* a_base, const RE::NiPoint3& a_pos,
                                                        const RE::NiPoint3& a_angle, bool a_keyframed,
                                                        RE::TESObjectREFR* a_origin = nullptr);

        void Clear();
        void Save(SKSE::SerializationInterface* a_intfc) const;
        void LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version);

    private:
        RE::TESObjectREFR* PlaceNearPlayer(const PlacedSpec& a_spec);

        mutable std::mutex                   lock_;
        std::unordered_map<Id, RE::FormID>   refs_;
    };
}
