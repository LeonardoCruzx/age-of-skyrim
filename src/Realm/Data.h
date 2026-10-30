#pragma once

#include "Core/Config.h"
#include "Realm/Modifiers.h"

namespace AoS::Realm
{
    // Definições do reino, todas data-driven e extensíveis por outros mods (cada pasta aceita vários *.json,
    // lidos em ordem alfabética; um id repetido num arquivo posterior SUBSTITUI o anterior -> patches de balanço):
    //   Doctrines/*.json  -> "doctrines", "offices", "governor", "defaultDoctrine"   (M18)
    //   Population/*.json -> "population"                                              (M19)
    //   Titles/*.json     -> "titles"                                                  (M22)
    // Textos: "nameKey"/"descriptionKey" (chave de Translations/*.json) ou "name" texto / { "en", "pt-br" }.

    // Custo de troca (M18). Sem economia ainda: o pagamento passa pelo gancho Government::SetPaymentHandler.
    struct Cost
    {
        std::string   resource;  // categoria do estoque (Resources.json): "gold", "food"...
        std::uint32_t amount{ 0 };
    };

    struct Doctrine
    {
        std::string       key;
        Id                id{ kNeutral };
        std::string       name;
        std::string       description;
        ModifierSet       modifiers{};
        Id                requiresTitle{ kNeutral };  // título mínimo para escolher (kNeutral = qualquer)
        std::vector<Cost> changeCost;                 // custo para TROCAR para esta doutrina
    };

    // Cargo da facção (mordomo, comandante, ferreiro...). O NPC na sede vem depois (M6): ver Government::AssignOfficer.
    struct Office
    {
        std::string key;
        Id          id{ kNeutral };
        std::string name;
        ModifierSet modifiers{};
        Id          requiresTitle{ kNeutral };
    };

    // Governador de província: bônus regional nos territórios da província.
    struct GovernorDef
    {
        ModifierSet modifiers{};
        float       majority{ 0.5f };  // fração de territórios da província que a facção precisa ter (estritamente mais)
    };

    struct PopulationDef
    {
        // Por TerritoryType, na ordem do enum (kUnknown..kHouse) — TEM que cobrir todo o enum: população_.initial[tipo]
        // indexa direto, sem checar limite (Data.cpp). claim (M8) e house (M29) são os dois últimos.
        std::array<float, 8> initial{ 10.0f, 60.0f, 15.0f, 25.0f, 120.0f, 800.0f, 5.0f, 20.0f };
        std::unordered_map<Id, float> overrides;  // por território ("territories": { "whiterun": 1200 })
        float capacityFactor{ 2.0f };     // teto = inicial × fator
        float growthPerDay{ 0.03f };      // crescimento logístico com comida (fração/dia)
        float starvationPerDay{ 0.05f };  // queda sem comida (fração/dia)
        float captureLoss{ 0.15f };       // perda ao trocar de dono (guerra)
        float draftReserve{ 0.25f };      // alistamento não desce abaixo desta fração da inicial
        float manpowerMin{ 0.5f };        // limites do multiplicador de produção por mão de obra
        float manpowerMax{ 1.5f };
    };

    // Marcos de um título (todos precisam ser atingidos).
    struct TitleRequirements
    {
        std::uint32_t territories{ 0 };
        std::uint32_t provinces{ 0 };   // províncias com maioria
        std::uint32_t cities{ 0 };
        std::uint32_t population{ 0 };  // população total
    };

    struct Title
    {
        std::string       key;
        Id                id{ kNeutral };
        std::string       name;
        std::string       nameFemale;  // "nameFemaleKey"/"nameFemale" (Rainha...); vazio = name
        int               rank{ 0 };
        TitleRequirements needs;
        ModifierSet       modifiers{};
        // Desbloqueios numéricos ("officers", "governors", "armyCap"...). Acumulados por rank no Load:
        // cada título herda os valores dos anteriores e sobrescreve os que declara.
        std::unordered_map<std::string, float> unlocks;
    };

    class Data
    {
    public:
        [[nodiscard]] static Data& Get() noexcept;

        // Chamar em kDataLoaded depois de Config::Load (idioma) e Factions::Load.
        void Load();

        [[nodiscard]] const std::vector<Doctrine>& Doctrines() const noexcept { return doctrines_; }
        [[nodiscard]] const std::vector<Office>&   Offices() const noexcept { return offices_; }
        [[nodiscard]] const std::vector<Title>&    Titles() const noexcept { return titles_; }  // ordem de rank
        [[nodiscard]] const GovernorDef&           Governor() const noexcept { return governor_; }
        [[nodiscard]] const PopulationDef&         Population() const noexcept { return population_; }
        [[nodiscard]] Id                           DefaultDoctrine() const noexcept { return defaultDoctrine_; }

        [[nodiscard]] const Doctrine* FindDoctrine(Id a_id) const;
        [[nodiscard]] const Office*   FindOffice(Id a_id) const;
        [[nodiscard]] const Title*    FindTitle(Id a_id) const;
        [[nodiscard]] int             RankOf(Id a_title) const;  // kNeutral/desconhecido -> -1

        // Territórios por província (fixo após o Config::Load): base da "maioria".
        [[nodiscard]] std::uint32_t ProvinceSize(Id a_province) const;
        [[nodiscard]] std::size_t   ProvinceCount() const noexcept { return provinceSize_.size(); }

        [[nodiscard]] float InitialPopulation(const Territory& a_territory) const;

    private:
        void LoadDoctrineFile(const nlohmann::json& a_json, std::string_view a_file);
        void LoadPopulationFile(const nlohmann::json& a_json);
        void LoadTitleFile(const nlohmann::json& a_json, std::string_view a_file);

        std::vector<Doctrine>                   doctrines_;
        std::vector<Office>                     offices_;
        std::vector<Title>                      titles_;
        GovernorDef                             governor_;
        PopulationDef                           population_;
        Id                                      defaultDoctrine_{ kNeutral };
        std::unordered_map<Id, std::uint32_t>   provinceSize_;
    };
}
