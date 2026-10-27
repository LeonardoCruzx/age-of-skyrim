#pragma once

#include "Core/Ids.h"

namespace AoS
{
    // Uma facção é dona de territórios e escolhe um estilo de bandeira (ADR 0004).
    //  - Pré-definidas: Data/SKSE/Plugins/AgeOfSkyrim/Factions/*.json (outros mods podem adicionar arquivos).
    //  - Do jogador: criadas em runtime e salvas no cosave (registro 'FACT' v1).
    struct Faction
    {
        Id            id{ kNeutral };
        std::string   key;      // chave de texto (pré-definidas) ou "player_<n>"
        std::string   name;     // nome exibido (já no idioma escolhido)
        Id            style{ kNeutral };
        std::uint32_t color{ 0xFFFFFF };  // 0xRRGGBB, para mapa/UI
        bool          player{ false };
    };

    class Factions
    {
    public:
        [[nodiscard]] static Factions& Get() noexcept;

        static constexpr std::uint32_t kRecord        = Tag("FACT");
        static constexpr std::uint32_t kRecordVersion = 1;

        // Lê os JSONs de facções pré-definidas. Chamar depois de Config::Load (valida os estilos).
        void Load();

        // Cópia da facção (nullopt se não existe). Cópia porque facções do jogador podem mudar em outra thread.
        [[nodiscard]] std::optional<Faction> Find(Id a_id) const;
        [[nodiscard]] std::vector<Faction>   All() const;           // pré-definidas, depois as do jogador
        [[nodiscard]] std::vector<Id>        Ids() const;
        [[nodiscard]] bool                   IsValidOwner(Id a_owner) const;  // neutro ou facção existente
        [[nodiscard]] Id                     StyleOf(Id a_owner) const;       // neutro -> kNeutral
        [[nodiscard]] std::string            NameOf(Id a_owner) const;        // nome exibido ("neutral" se neutro)
        [[nodiscard]] std::optional<Faction> PlayerFaction() const;           // a primeira do jogador

        // Facção do jogador (estilo precisa existir). Retorna o Id, ou nullopt se o estilo é inválido.
        std::optional<Id> CreatePlayerFaction(std::string a_name, Id a_style, std::uint32_t a_color);
        bool              Rename(Id a_id, std::string a_name);
        bool              SetStyle(Id a_id, Id a_style);
        bool              SetColor(Id a_id, std::uint32_t a_color);

        // Migração do TERR v1, em que o dono era um estilo: primeira pré-definida com esse estilo.
        [[nodiscard]] Id FromLegacyStyle(Id a_style) const;

        void ClearPlayerFactions();
        void Save(SKSE::SerializationInterface* a_intfc) const;
        void LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version);

    private:
        void LoadFile(const fs::path& a_file);
        [[nodiscard]] const Faction* FindUnlocked(Id a_id) const;
        Faction*                     FindPlayerUnlocked(Id a_id);

        mutable std::mutex   lock_;
        std::vector<Faction> predefined_;
        std::vector<Faction> player_;
        std::uint32_t        nextPlayer_{ 1 };
    };
}
