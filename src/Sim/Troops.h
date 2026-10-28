#pragma once

#include "Core/Config.h"

namespace AoS
{
    // M20/M21: tabela de tropas de CAMPO (exércitos recrutados e batalhas), separada da guarnição (M14).
    //  - Mesmo formato/estratégia de escolha de ActorBase que Garrison.json/Garrison.cpp já usa (ADR 0044):
    //    candidatos explícitos ("Plugin.esp|0xID" ou EditorID) e, sem nenhum válido, escolha automática de um
    //    NPC genérico do Skyrim.esm (FaceGen vanilla pronto) pela dica de armadura do outfit padrão.
    //  - Decisão (ADR 0064): a lógica de resolução fica DUPLICADA aqui (não extraída de Garrison.cpp) para não
    //    arriscar regressão num módulo estável e fora do escopo desta missão; o formato do JSON é o mesmo.
    //  - Usada pela M20 (custo/tempo de treino/força de cada tipo) e pela M21 (ActorBase + outfit dos atores
    //    de batalha). Dados em Data/SKSE/Plugins/AgeOfSkyrim/Troops.json + Troops/*.json (outros mods).
    class Troops
    {
    public:
        struct Troop
        {
            std::string                  key;
            RE::TESBoundObject*          actor{ nullptr };  // TESNPC ou TESLevCharacter (FaceGen vanilla pronto)
            RE::BGSOutfit*               outfit{ nullptr };  // nullptr = outfit padrão da base (reaplicado pelo DLL)
            double                       strength{ 1.0 };     // força por soldado (resolução numérica, M16/M21)
            double                       goldCost{ 10.0 };    // custo de ouro por soldado recrutado
            std::string                  equipCategory;       // categoria do estoque (Resources.json); "" = nenhuma
            std::int64_t                 equipAmount{ 0 };    // itens da categoria por soldado
            std::uint32_t                trainHours{ 12 };    // horas de jogo até o soldado entrar no exército
            std::vector<std::pair<Id, double>> upkeep;        // recurso -> por soldado por hora (M20)
        };

        [[nodiscard]] static Troops& Get() noexcept;

        // kDataLoaded, depois de Config/Factions/Stock/Economy: lê Troops.json e resolve os ActorBase.
        void Load();

        // Facção -> estilo -> "default" (mesma ordem de busca de Garrison::TroopFor).
        [[nodiscard]] const Troop* TroopFor(Id a_faction) const;
        [[nodiscard]] const Troop* Find(Id a_troop) const;  // pela chave (MakeId do "key")
        [[nodiscard]] std::vector<Id> Keys() const;

        // Outfit aplicado pelo DLL (lição "tropas peladas", ADR 0044): nunca edita a base vanilla.
        void EnsureOutfit(RE::Actor* a_actor, const Troop& a_troop) const;

    private:
        void LoadFile(const fs::path& a_file);
        void ResolveTroop(const std::string& a_key, const nlohmann::json& a_json);

        std::unordered_map<Id, Troop> troops_;
    };
}
