#pragma once

#include "Core/Ids.h"

#include <deque>
#include <shared_mutex>

namespace AoS
{
    // "Plugin.esp|0x00ABCD": ID *local* do form (sem o índice de load order).
    struct FormRef
    {
        std::string   plugin;
        RE::FormID    localId{ 0 };

        [[nodiscard]] static std::optional<FormRef> Parse(std::string_view a_text);

        // nullptr se o plugin não está carregado ou o form não é do tipo pedido.
        template <class T = RE::TESObjectREFR>
        [[nodiscard]] T* Resolve() const
        {
            const auto dh = RE::TESDataHandler::GetSingleton();
            return dh ? dh->LookupForm<T>(localId, plugin) : nullptr;
        }
    };

    // Objeto criado pelo DLL em runtime (sem editar células no ESP). A referência criada fica no cosave
    // (ver World/Placement). Fase atual: posição relativa ao jogador no momento da criação (só para teste).
    struct PlacedSpec
    {
        Id          key{ kNeutral };     // "território/estilo/índice" -> chave estável no cosave
        FormRef     base;                // objeto base (static, misc item...)
        RE::NiPoint3 offset;             // x = direita, y = frente, z = cima, relativo ao jogador
    };

    // Banners de UM estilo (heráldica) dentro de UM território. Um forte pode ter várias bandeiras.
    struct BannerSet
    {
        Id                      style{ kNeutral };
        std::vector<FormRef>    refs;    // pré-colocadas num plugin
        std::vector<PlacedSpec> placed;  // criadas em runtime
    };

    // Tipo do local (mapa tático, IA, economia). "tower" só vem do JSON: o jogo não tem keyword de torre.
    enum class TerritoryType : std::uint8_t
    {
        kUnknown,
        kFort,
        kTower,
        kCamp,
        kVillage,
        kCity,
        kClaim,  // M8: estandarte do Free Claim (território dinâmico, criado em runtime)
    };

    [[nodiscard]] std::string_view             ToString(TerritoryType a_type) noexcept;
    [[nodiscard]] std::optional<TerritoryType> ParseTerritoryType(std::string_view a_text) noexcept;

    struct Territory
    {
        std::string            key;
        Id                     id{ kNeutral };
        std::string            name;
        Id                     defaultOwner{ kNeutral };  // facção
        std::vector<BannerSet> banners;

        // ---- M4: geografia (auto-descoberta ou JSON) ----
        bool                   discovered{ false };     // criado pela descoberta (não veio de um JSON)
        std::optional<FormRef> locationRef;             // JSON: "location": "Skyrim.esm|0xID"
        RE::BGSLocation*       location{ nullptr };     // resolvido em runtime
        TerritoryType          type{ TerritoryType::kUnknown };
        Id                     province{ kNeutral };    // Hold (LocTypeHold) acima do local
        std::string            provinceName;
        bool                   hasPosition{ false };
        RE::NiPoint3           position;                // marcador do mapa (unidades do jogo)
        RE::FormID             worldspace{ 0 };         // vizinhos só no mesmo worldspace
        std::vector<Id>        neighbors;               // k mais próximos, calculado no load
    };

    // settings.json -> "discovery": territórios a partir dos BGSLocation do jogo (inclui os de outros mods).
    struct DiscoverySettings
    {
        bool                       enabled{ true };
        std::vector<TerritoryType> types{ TerritoryType::kFort, TerritoryType::kCamp, TerritoryType::kVillage,
                                          TerritoryType::kCity };
        std::unordered_set<Id>     exclude;             // MakeId("Plugin.esp|0x00ABCD")
        std::size_t                neighbors{ 4 };
    };

    struct DebugSettings
    {
        bool enabled{ false };
        int  hotkey{ 0x44 };          // DIK_F10: alterna o dono
        int  selfTestHotkey{ 0x57 };  // DIK_F11: autoteste
        int  stockHotkey{ 0x42 };     // DIK_F8: estoque do inventário do jogador por categoria
        int  claimHotkey{ 0x3D };     // DIK_F3: Free Claim (fincar estandarte); F9 é o quickload do jogo
        // ---- M12/M13: economia e rotas ----
        int           economyHotkey{ 0x40 };  // DIK_F6: força N horas de tick e mostra o resumo
        int           routeHotkey{ 0x41 };    // DIK_F7: rota entre os 2 territórios mais próximos do jogador
        std::uint32_t economyHours{ 24 };     // horas forçadas pelo economyHotkey
        // ---- fim M12/M13 ----
        int  conquestHotkey{ 0x3E };  // DIK_F4: simula a conquista do local atual (M5)
        int  warMapHotkey{ 0x49 };    // DIK_NUMPAD9: abre a mesa de guerra de qualquer lugar (M11)
        // ---- M14/M15: guarnição e melhorias ----
        int  garrisonHotkey{ 0x4C };    // DIK_NUMPAD5: guarnição do território atual + spawn forçado perto do jogador
        int  upgradeHotkey{ 0x4D };     // DIK_NUMPAD6: inicia a próxima melhoria disponível no território atual/sede
        bool upgradeIgnoreCost{ false };  // true: a hotkey de melhoria não cobra itens (só para testar)
        // ---- fim M14/M15 ----
        // ---- M16/M17: IA e diplomacia ----
        int aiHotkey{ 0x47 };          // DIK_NUMPAD7: força o tick estratégico e mostra os ataques ativos
        int diplomacyHotkey{ 0x48 };   // DIK_NUMPAD8: alterna a relação do jogador com o dono do território atual
        // ---- fim M16/M17 ----
        // ---- M25/M26: lei/prisioneiros e ordem pública/rebeliões ----
        int lawCaptureHotkey{ 0x3B };   // DIK_F1: simula lawDebugCaptureCount capturas no território atual
        int lawRansomHotkey{ 0x3C };    // DIK_F2: resgata (ouro) o 1º prisioneiro da facção do jogador
        int lawExecuteHotkey{ 0x58 };   // DIK_F12: executa o 1º prisioneiro da facção do jogador
        int stabilityHotkey{ 0x52 };    // DIK_NUMPAD0: estabilidade do território atual + checagem de rebelião
        // ---- fim M25/M26 ----
        Id   territory{ kNeutral };

        // M18/M19/M22 (src/Realm): resumo do reino, simular horas, ciclar doutrina, conquistar locais próximos.
        int   realmHotkey{ 0x4F };          // DIK_NUMPAD1: resumo do reino na tela
        int   realmHoursHotkey{ 0x50 };     // DIK_NUMPAD2: simula realmDebugHours horas (Realm::OnHoursPassed)
        int   realmDoctrineHotkey{ 0x51 };  // DIK_NUMPAD3: troca para a próxima doutrina (custo/espera valem)
        int   realmConquerHotkey{ 0x4B };   // DIK_NUMPAD4: dá à facção do jogador os N locais mais próximos
        float realmDebugHours{ 24.0f };
        int   realmConquerCount{ 5 };
    };

    // settings.json -> "realm": governo/doutrina (M18), população (M19), títulos (M22).
    struct RealmSettings
    {
        float doctrineCooldownHours{ 168.0f };  // horas de jogo entre trocas de doutrina (a 1ª escolha é livre)
        bool  announceTitles{ true };           // anúncio na tela ao subir de título
    };

    // ---- M14: guarnição (ADR 0043/0044) -----------------------------------------------------------
    // settings.json -> "garrison": materialização dos soldados perto do jogador. Números e tropas em Garrison.json.
    struct GarrisonSettings
    {
        bool          enabled{ true };       // false: só números (nenhum ator é criado)
        std::uint32_t maxActors{ 12 };       // teto GLOBAL de soldados de guarnição vivos no mundo
        std::uint32_t spawnPerFrame{ 2 };    // atores criados por frame (lotes encadeados via AddTask)
        float         spawnRadius{ 8192.0f };  // marcador do território até esta distância do jogador (unidades)
        float         spreadRadius{ 256.0f };  // raio do círculo em volta do marcador onde os soldados nascem
    };
    // ---- fim M14 ----------------------------------------------------------------------------------

    // settings.json -> "claim": Free Claim (estandarte em local genérico).
    struct ClaimSettings
    {
        FormRef banner{ "Skyrim.esm", 0x012EB7 };  // objeto base do estandarte (padrão: Iron Sword, fincada)
        float   minDistance{ 2048.0f };            // unidades do jogo entre claims (~29 m)
        int     maxClaims{ 10 };
        float   maxSlopeDegrees{ 35.0f };          // inclinação máxima do terreno
        float   forwardDistance{ 150.0f };         // à frente do jogador
    };

    // ---- M5/M6 (conquista e sede) -------------------------------------------------------------
    // settings.json -> "conquest": conquista por local limpo (ADR 0022).
    struct ConquestSettings
    {
        bool                     enabled{ true };
        bool                     requirePlayerInLocation{ true };  // false: vale também o local de onde acabou de sair
        bool                     onlyFromNeutral{ false };         // true: não toma locais de outras facções
        std::unordered_set<Id>   exclude;                          // MakeId("Plugin.esp|0x00ABCD") ou id de território
        std::vector<std::string> excludePlugins;                   // locais destes plugins são ignorados (minúsculas)
        bool                     skipCivilWarForts{ false };       // M16/ADR 0042: fortes LocTypeMilitaryFort ficam de fora
        std::string              defaultStyle;                     // facção criada na 1ª conquista; vazio = automático
        std::uint32_t            defaultColor{ 0x2E7D32 };
    };

    // settings.json -> "seat": sede da facção (ADR 0023).
    struct SeatSettings
    {
        bool                   askBeforeFounding{ true };
        std::optional<FormRef> chest;     // objeto base do baú; nullopt = escolha automática (sem respawn)
        std::optional<FormRef> warTable;  // objeto base da mesa de guerra; nullopt = escolha automática
        RE::NiPoint3           chestOffset{ -100.0f, 150.0f, 0.0f };     // direita, frente, cima (relativo ao jogador)
        RE::NiPoint3           warTableOffset{ 120.0f, 220.0f, 0.0f };
    };
    // ---- fim M5/M6 ------------------------------------------------------------------------------

    // settings.json -> "api": integração com outros mods (ADR 0032).
    struct ApiSettings
    {
        bool papyrusEvents{ true };      // ModEvents "AoS_*" para scripts Papyrus
        bool allowOwnerChanges{ true };  // outros mods (C++/Papyrus) podem trocar o dono de territórios
    };
    // ---- M11: mesa de guerra / mapa tático (ADR 0051-0054) ----
    // settings.json -> "warMap".
    struct WarMapSettings
    {
        bool          enabled{ true };
        bool          usePrismaUI{ true };                    // false: sempre os menus vanilla
        std::string   view{ "AgeOfSkyrim/index.html" };       // relativo a Data/PrismaUI/views/
        bool          pauseGame{ true };
        std::uint32_t pageSize{ 6 };                          // menus vanilla: territórios por página
        float         nearbyRadius{ 60000.0f };               // menus vanilla: "próximos" (0 = sem limite)
        std::uint32_t maxTargets{ 10 };                       // alvos de ataque marcados
    };
    // ---- fim M11 ----
    // ---- M16/M17 (IA e diplomacia) ---------------------------------------------------------------
    // settings.json -> "ai": facções de IA e contra-ataques (ADR 0047).
    struct AiSettings
    {
        bool          enabled{ true };
        std::uint32_t tickHours{ 6 };               // horas de jogo entre avaliações estratégicas
        std::size_t   maxEvaluationsPerTick{ 512 }; // territórios examinados por avaliação (lote)
        std::size_t   maxActiveAttacks{ 8 };        // teto global de ataques em andamento
        std::size_t   maxAttacksPerFaction{ 2 };
        std::uint32_t cooldownHours{ 24 };          // espera da facção entre dois ataques lançados
        double        attackBase{ 20.0 };           // força de um ataque sem gastar ouro
        double        goldPerStrength{ 10.0 };      // ouro do saldo da IA por ponto de força
        double        minGoldPerAttack{ 50.0 };     // a IA só ataca se puder pagar isto
        double        maxGoldPerAttack{ 500.0 };
        double        minAdvantage{ 1.2 };          // força >= defesa × isto para lançar
        double        randomness{ 0.2 };            // ± na resolução (0 = determinística)
        double        hoursPer10kUnits{ 2.0 };      // marcha: horas por 10000 unidades (≈ 143 m)
        std::uint32_t minTravelHours{ 2 };
        std::uint32_t maxTravelHours{ 48 };
        float         nearPlayerRadius{ 8192.0f };  // ataque "perto do jogador" (gancho da M21)
        bool          attackPlayer{ true };
        bool          expandIntoNeutral{ true };
        std::vector<TerritoryType> expandTypes{ TerritoryType::kFort, TerritoryType::kVillage, TerritoryType::kCity };
        double        neutralDefenseMultiplier{ 0.5 };
        double        declareWarRatio{ 2.0 };       // poder >= alvo × isto para a IA declarar guerra por agressão
        bool          respectConquestExclusions{ true };  // locais excluídos da conquista (settings + Compat)
        bool          skipCivilWarForts{ false };         // fortes da Guerra Civil vanilla nunca são alvo da IA
        std::unordered_map<TerritoryType, double> defenseByType{
            { TerritoryType::kUnknown, 15.0 }, { TerritoryType::kFort, 40.0 },    { TerritoryType::kTower, 25.0 },
            { TerritoryType::kCamp, 15.0 },    { TerritoryType::kVillage, 15.0 }, { TerritoryType::kCity, 60.0 },
            { TerritoryType::kClaim, 10.0 }
        };
    };

    // settings.json -> "diplomacy": relações entre facções (ADR 0049).
    struct DiplomacySettings
    {
        std::uint32_t truceHours{ 72 };          // paz aceita depois de guerra = trégua por N horas, depois paz
        double        peaceAcceptRatio{ 1.5 };   // IA aceita paz se poder(IA) < poder(proponente) × isto
        double        allianceAcceptRatio{ 0.75 };  // aliança: poder(proponente) >= poder(IA) × isto
        bool          allianceNeedsCommonEnemy{ true };
        bool          warOnConquest{ true };     // tomar território de uma facção em paz = guerra com ela
    };
    // ---- fim M16/M17 ------------------------------------------------------------------------------

    // ---- M25: lei e prisioneiros de guerra (ADR 0060/0061) ----------------------------------------
    // settings.json -> "law". Facção de crime vanilla do Hold (bounty): touchCrimeFaction é o interruptor
    // mestre (default false = nunca toca, compatibilidade com mods de Guerra Civil/lei, ADR 0005); só com
    // ele ligado é que resetBounty tem efeito. Prisioneiros: captureFraction dos soldados derrotados num
    // combate (M16) vira prisioneiro do vencedor (Law::CaptureFromCombat), até a capacidade da facção
    // (baseCapacityPerTerritory por território + melhoria "prison" do Upgrades, Stat::kPrisonCapacity).
    struct LawSettings
    {
        bool          touchCrimeFaction{ false };       // default: nunca toca (compat)
        bool          resetBounty{ true };               // ação ao trocar de dono, se touchCrimeFaction
        double        captureFraction{ 0.5 };             // fração dos derrotados que vira prisioneiro
        std::uint32_t baseCapacityPerTerritory{ 3 };      // teto antes de melhorias
        double        ransomCostPerPrisoner{ 50.0 };      // ouro por prisioneiro resgatado
        std::uint32_t debugCaptureCount{ 3 };             // prisioneiros gerados pela hotkey de captura simulada
    };
    // ---- fim M25 ------------------------------------------------------------------------------------

    // ---- M26: ordem pública e rebeliões (ADR 0062/0063) -------------------------------------------
    // settings.json -> "stability". Estabilidade (0-100) por território: baseline × Modifiers::GetFor(Stability)
    // (doutrina/governador) menos penalidades de guarnição insuficiente (Garrison::Strength vs população ×
    // popToStrength) e de déficit (Economy::InDeficit, comida OU ouro). Abaixo de rebellionThreshold por
    // graceHours seguidas, chancePerHour por hora de virar rebelião (SetTerritoryOwner para a facção "rebels"
    // ou kNeutral, conforme rebelFaction).
    struct StabilitySettings
    {
        bool          enabled{ true };
        double        baseline{ 60.0 };
        double        popToStrength{ 0.05 };    // força de guarnição esperada por habitante
        double        garrisonPenalty{ 25.0 };  // pontos perdidos no máximo por guarnição insuficiente
        double        deficitPenalty{ 15.0 };   // pontos perdidos por déficit (comida OU ouro, cada)
        double        rebellionThreshold{ 35.0 };
        std::uint32_t graceHours{ 48 };          // horas seguidas abaixo do limiar antes de poder rebelar
        double        chancePerHour{ 0.02 };     // chance por hora, depois da graça
        bool          rebelFaction{ true };      // true: facção "rebels"; false: kNeutral
        double        unrestDecayPerHour{ 0.5 }; // decaimento do abalo de execuções (Law::Execute)
    };
    // ---- fim M26 ------------------------------------------------------------------------------------

    class Config
    {
    public:
        [[nodiscard]] static Config& Get() noexcept;

        // Lê settings.json e todos os Territories/*.json de Data/SKSE/Plugins/AgeOfSkyrim/.
        bool Load();

        [[nodiscard]] const Territory*              Find(Id a_territory) const;
        [[nodiscard]] const Territory*              FindByLocation(const RE::BGSLocation* a_location) const;  // O(1)
        [[nodiscard]] const std::vector<Territory>& Territories() const noexcept { return territories_; }
        [[nodiscard]] const std::vector<Id>&        Styles() const noexcept { return styles_; }
        [[nodiscard]] std::string_view              NameOf(Id a_id) const;  // para logs
        [[nodiscard]] const DebugSettings&          Debug() const noexcept { return debug_; }
        [[nodiscard]] const ClaimSettings&          Claim() const noexcept { return claim_; }
        [[nodiscard]] const DiscoverySettings&      Discovery() const noexcept { return discovery_; }
        [[nodiscard]] const ConquestSettings&       Conquest() const noexcept { return conquest_; }  // M5
        [[nodiscard]] const SeatSettings&           Seat() const noexcept { return seat_; }          // M6
        [[nodiscard]] const WarMapSettings&         WarMap() const noexcept { return warMap_; }      // M11
        [[nodiscard]] const GarrisonSettings&       Garrison() const noexcept { return garrison_; }  // M14
        [[nodiscard]] const AiSettings&             Ai() const noexcept { return ai_; }              // M16
        [[nodiscard]] const DiplomacySettings&      Diplomacy() const noexcept { return diplomacy_; } // M17
        [[nodiscard]] const LawSettings&            Law() const noexcept { return law_; }             // M25
        [[nodiscard]] const StabilitySettings&      Stability() const noexcept { return stability_; } // M26

        // M16: dono padrão vindo dos "start" das facções (Factions/*.json). Só vale se o JSON do território não
        // definiu defaultOwner. Chamar em kDataLoaded, antes de State::ResetToDefaults.
        void SetDefaultOwner(Id a_territory, Id a_owner);

        // ---- M8: territórios dinâmicos (claims), por save, criados em runtime (ADR 0024) ----
        // Territories() continua só com os do JSON/descoberta; quem quer ver todos usa ForEachTerritory.
        // Find() enxerga os dois. Ponteiros para dinâmicos ficam válidos até ClearDynamic (revert/novo load).
        // Add/Clear só na thread principal (ou no callback de load do cosave).
        const Territory* AddDynamic(Territory a_territory);  // completa província/vizinhos pelos estáticos
        void             ClearDynamic();

        template <class F>
        void ForEachTerritory(F&& a_fn) const
        {
            for (const auto& t : territories_) {
                a_fn(t);
            }
            std::shared_lock l{ dynamicLock_ };
            for (const auto& t : dynamic_) {
                a_fn(t);
            }
        }
        [[nodiscard]] const RealmSettings&          Realm() const noexcept { return realm_; }
        // Posição do território em Territories() (O(1)); nullopt se desconhecido.
        [[nodiscard]] std::optional<std::size_t>    IndexOf(Id a_territory) const;
        [[nodiscard]] const ApiSettings&            Api() const noexcept { return api_; }

    private:
        void LoadSettings(const fs::path& a_file);
        void LoadTerritoryFile(const fs::path& a_file);
        void LoadConquestAndSeat(const nlohmann::json& a_json);  // M5/M6
        void LoadAiAndDiplomacy(const nlohmann::json& a_json);   // M16/M17
        void LoadLawAndStability(const nlohmann::json& a_json);  // M25/M26
        void LoadCompat();                                       // M16: Compat/*.json (ADR 0042)
        void RegisterName(Id a_id, const std::string& a_key);
        void MergeDiscovered();   // M4: territórios do jogo + vínculo dos JSON com "location"
        void ComputeNeighbors();

        std::vector<Territory>                  territories_;
        std::unordered_map<Id, std::size_t>     index_;
        std::vector<Id>                         styles_;
        std::unordered_map<Id, std::string>     names_;
        DebugSettings                           debug_;
        ClaimSettings                           claim_;
        DiscoverySettings                       discovery_;
        ConquestSettings                        conquest_;
        SeatSettings                            seat_;
        RealmSettings                           realm_;
        ApiSettings                             api_;
        WarMapSettings                          warMap_;  // M11
        GarrisonSettings                        garrison_;  // M14
        AiSettings                              ai_;          // M16
        DiplomacySettings                       diplomacy_;   // M17
        LawSettings                             law_;         // M25
        StabilitySettings                       stability_;   // M26
        bool                                    userSkipCwConquest_{ false };  // definido no settings.json?
        bool                                    userSkipCwAi_{ false };
        std::unordered_map<const RE::BGSLocation*, std::size_t> byLocation_;

        // M8: territórios dinâmicos (deque: push_back não invalida ponteiros).
        mutable std::shared_mutex                         dynamicLock_;
        std::deque<Territory>                             dynamic_;
        std::unordered_map<Id, const Territory*>          dynamicIndex_;
    };
}
