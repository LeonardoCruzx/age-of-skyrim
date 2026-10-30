#include "Core/Config.h"

#include "Core/I18n.h"
#include "Core/Timer.h"
#include "World/Discovery.h"

namespace AoS
{
    namespace
    {
        const fs::path kRoot = "Data/SKSE/Plugins/AgeOfSkyrim";

        std::optional<nlohmann::json> ReadJson(const fs::path& a_file)
        {
            std::ifstream in(a_file);
            if (!in) {
                return std::nullopt;
            }
            try {
                return nlohmann::json::parse(in, nullptr, true, /*ignore_comments*/ true);
            } catch (const std::exception& e) {
                logger::error("JSON inválido em {}: {}", a_file.string(), e.what());
                return std::nullopt;
            }
        }
    }

    // ---- FormRef --------------------------------------------------------------------------------

    std::optional<FormRef> FormRef::Parse(std::string_view a_text)
    {
        const auto bar = a_text.find('|');
        if (bar == std::string_view::npos || bar == 0) {
            return std::nullopt;
        }
        auto hex = a_text.substr(bar + 1);
        if (hex.starts_with("0x") || hex.starts_with("0X")) {
            hex.remove_prefix(2);
        }
        std::uint32_t value{};
        const auto [ptr, ec] = std::from_chars(hex.data(), hex.data() + hex.size(), value, 16);
        if (ec != std::errc{} || ptr != hex.data() + hex.size()) {
            return std::nullopt;
        }
        return FormRef{ std::string(a_text.substr(0, bar)), value };
    }

    // ---- TerritoryType --------------------------------------------------------------------------

    std::string_view ToString(TerritoryType a_type) noexcept
    {
        switch (a_type) {
        case TerritoryType::kFort:    return "fort";
        case TerritoryType::kTower:   return "tower";
        case TerritoryType::kCamp:    return "camp";
        case TerritoryType::kVillage: return "village";
        case TerritoryType::kCity:    return "city";
        case TerritoryType::kClaim:   return "claim";
        case TerritoryType::kHouse:   return "house";
        default:                      return "unknown";
        }
    }

    std::optional<TerritoryType> ParseTerritoryType(std::string_view a_text) noexcept
    {
        for (const auto t : { TerritoryType::kFort, TerritoryType::kTower, TerritoryType::kCamp,
                              TerritoryType::kVillage, TerritoryType::kCity, TerritoryType::kClaim,
                              TerritoryType::kHouse }) {
            if (_strnicmp(a_text.data(), ToString(t).data(), a_text.size()) == 0 && a_text.size() == ToString(t).size()) {
                return t;
            }
        }
        return std::nullopt;
    }

    // ---- Config ---------------------------------------------------------------------------------

    Config& Config::Get() noexcept
    {
        static Config instance;
        return instance;
    }

    void Config::RegisterName(Id a_id, const std::string& a_key)
    {
        const auto [it, inserted] = names_.try_emplace(a_id, a_key);
        if (!inserted && _stricmp(it->second.c_str(), a_key.c_str()) != 0) {
            logger::error("Colisão de hash entre '{}' e '{}': renomeie um deles.", it->second, a_key);
        }
    }

    bool Config::Load()
    {
        territories_.clear();
        index_.clear();
        byLocation_.clear();
        styles_.clear();
        names_.clear();

        LoadSettings(kRoot / "settings.json");
        LoadCompat();  // M16: depois do settings.json, antes da descoberta (usa discovery.exclude)

        const auto dir = kRoot / "Territories";
        std::error_code ec;
        std::vector<fs::path> files;
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                files.push_back(entry.path());
            }
        }
        std::sort(files.begin(), files.end());  // ordem determinística entre arquivos
        for (const auto& f : files) {
            LoadTerritoryFile(f);
        }
        MergeDiscovered();
        Discovery::LogSummary();
        if (debug_.enabled) {
            Discovery::WriteDump();
        }

        logger::info("Config: {} território(s), {} estilo(s) de bandeira.", territories_.size(), styles_.size());
        return !territories_.empty();
    }

    void Config::LoadSettings(const fs::path& a_file)
    {
        const auto json = ReadJson(a_file);
        // Idioma antes de tudo: nomes de territórios são escolhidos na leitura.
        I18n::Load(json ? json->value("language", "auto"s) : "auto"s,
                   json ? json->value("textEncoding", "utf8"s) : "utf8"s);
        if (!json) {
            return;  // settings.json é opcional
        }
        if (const auto dbg = json->find("debug"); dbg != json->end() && dbg->is_object()) {
            debug_.enabled        = dbg->value("enabled", false);
            debug_.hotkey         = dbg->value("hotkey", debug_.hotkey);
            debug_.menuHotkey     = dbg->value("menuHotkey", debug_.menuHotkey);  // M30
            debug_.selfTestHotkey = dbg->value("selfTestHotkey", debug_.selfTestHotkey);
            debug_.stockHotkey    = dbg->value("stockHotkey", debug_.stockHotkey);
            debug_.claimHotkey    = dbg->value("claimHotkey", debug_.claimHotkey);
            // ---- M12/M13: economia e rotas ----
            debug_.economyHotkey = dbg->value("economyHotkey", debug_.economyHotkey);
            debug_.routeHotkey   = dbg->value("routeHotkey", debug_.routeHotkey);
            debug_.economyHours  = dbg->value("economyHours", debug_.economyHours);
            // ---- fim M12/M13 ----
            debug_.realmHotkey         = dbg->value("realmHotkey", debug_.realmHotkey);
            debug_.realmHoursHotkey    = dbg->value("realmHoursHotkey", debug_.realmHoursHotkey);
            debug_.realmDoctrineHotkey = dbg->value("realmDoctrineHotkey", debug_.realmDoctrineHotkey);
            debug_.realmConquerHotkey  = dbg->value("realmConquerHotkey", debug_.realmConquerHotkey);
            debug_.realmDebugHours     = dbg->value("realmDebugHours", debug_.realmDebugHours);
            debug_.realmConquerCount   = dbg->value("realmConquerCount", debug_.realmConquerCount);
            debug_.warMapHotkey   = dbg->value("warMapHotkey", debug_.warMapHotkey);  // M11
            // ---- M14/M15: guarnição e melhorias ----
            debug_.garrisonHotkey    = dbg->value("garrisonHotkey", debug_.garrisonHotkey);
            debug_.upgradeHotkey     = dbg->value("upgradeHotkey", debug_.upgradeHotkey);
            debug_.upgradeIgnoreCost = dbg->value("upgradeIgnoreCost", debug_.upgradeIgnoreCost);
            // ---- fim M14/M15 ----
            debug_.aiHotkey        = dbg->value("aiHotkey", debug_.aiHotkey);                // M16
            debug_.diplomacyHotkey = dbg->value("diplomacyHotkey", debug_.diplomacyHotkey);  // M17
            debug_.jarlHotkey      = dbg->value("jarlHotkey", debug_.jarlHotkey);             // M24
            // ---- M25/M26: lei/prisioneiros e ordem pública/rebeliões ----
            debug_.lawCaptureHotkey = dbg->value("lawCaptureHotkey", debug_.lawCaptureHotkey);
            debug_.lawRansomHotkey  = dbg->value("lawRansomHotkey", debug_.lawRansomHotkey);
            debug_.lawExecuteHotkey = dbg->value("lawExecuteHotkey", debug_.lawExecuteHotkey);
            debug_.stabilityHotkey  = dbg->value("stabilityHotkey", debug_.stabilityHotkey);
            // ---- fim M25/M26 ----
            // ---- M20/M21: exército e batalhas ----
            debug_.armyRecruitHotkey = dbg->value("armyRecruitHotkey", debug_.armyRecruitHotkey);
            debug_.armyListHotkey    = dbg->value("armyListHotkey", debug_.armyListHotkey);
            debug_.battleTestHotkey  = dbg->value("battleTestHotkey", debug_.battleTestHotkey);
            debug_.armyRecruitCount  = dbg->value("armyRecruitCount", debug_.armyRecruitCount);
            debug_.battleTestSize    = dbg->value("battleTestSize", debug_.battleTestSize);
            // ---- fim M20/M21 ----
            if (const auto t = dbg->value("territory", std::string{}); !t.empty()) {
                debug_.territory = MakeId(t);
            }
        }
        if (const auto c = json->find("claim"); c != json->end() && c->is_object()) {
            if (const auto b = FormRef::Parse(c->value("banner", std::string{}))) {
                claim_.banner = *b;
            }
            claim_.minDistance     = c->value("minDistance", claim_.minDistance);
            claim_.maxClaims       = c->value("maxClaims", claim_.maxClaims);
            claim_.maxSlopeDegrees = c->value("maxSlopeDegrees", claim_.maxSlopeDegrees);
            claim_.forwardDistance = c->value("forwardDistance", claim_.forwardDistance);
        }
        if (const auto r = json->find("realm"); r != json->end() && r->is_object()) {
            realm_.doctrineCooldownHours = std::max(0.0f, r->value("doctrineCooldownHours", realm_.doctrineCooldownHours));
            realm_.announceTitles        = r->value("announceTitles", realm_.announceTitles);
            // M27 (ADR 0057): fração de províncias com maioria para "Dominador(a) de Skyrim" (1.0 = todas).
            realm_.dominationThreshold =
                std::clamp(r->value("dominationThreshold", realm_.dominationThreshold), 0.0f, 1.0f);
        }
        // ---- M24: presença do governador na capital da província (ADR 0056) ----
        if (const auto j = json->find("jarl"); j != json->end() && j->is_object()) {
            jarl_.askBeforeAssign = j->value("askBeforeAssign", jarl_.askBeforeAssign);
            // "auto" (ou ausente) = escolha automática, igual ao padrão do bloco "seat".
            if (const auto b = j->value("actor", "auto"s); b != "auto") {
                jarl_.actor = FormRef::Parse(b);
                if (!jarl_.actor) {
                    logger::warn("settings.json: jarl.actor inválido; usando escolha automática.");
                }
            }
            if (const auto b = j->value("outfit", "auto"s); b != "auto") {
                jarl_.outfit = FormRef::Parse(b);
                if (!jarl_.outfit) {
                    logger::warn("settings.json: jarl.outfit inválido; usando o outfit padrão do NPC.");
                }
            }
            jarl_.autoHint = j->value("autoHint", jarl_.autoHint);
            if (const auto v = j->value("offset", std::vector<float>{}); v.size() == 3) {
                jarl_.offset = { v[0], v[1], v[2] };
            }
        }
        // ---- fim M24 ----
        if (const auto a = json->find("api"); a != json->end() && a->is_object()) {
            api_.papyrusEvents     = a->value("papyrusEvents", api_.papyrusEvents);
            api_.allowOwnerChanges = a->value("allowOwnerChanges", api_.allowOwnerChanges);
        }
        if (const auto d = json->find("discovery"); d != json->end() && d->is_object()) {
            discovery_.enabled   = d->value("enabled", true);
            discovery_.neighbors = d->value("neighbors", std::size_t{ 4 });
            if (const auto types = d->find("types"); types != d->end() && types->is_array()) {
                discovery_.types.clear();
                for (const auto& v : *types) {
                    const auto type = v.is_string() ? ParseTerritoryType(v.get<std::string>()) : std::nullopt;
                    if (type) {
                        discovery_.types.push_back(*type);
                    } else {
                        logger::warn("settings.json: tipo de descoberta inválido ignorado.");
                    }
                }
            }
            if (const auto ex = d->find("exclude"); ex != d->end() && ex->is_array()) {
                for (const auto& v : *ex) {
                    // Normaliza ("Skyrim.esm|0x1A2B" == "skyrim.esm|0x001a2b") para o mesmo formato de KeyOf.
                    const auto ref = v.is_string() ? FormRef::Parse(v.get<std::string>()) : std::nullopt;
                    if (ref) {
                        discovery_.exclude.insert(MakeId(std::format("{}|0x{:06X}", ref->plugin, ref->localId)));
                    }
                }
            }
        }
        LoadConquestAndSeat(*json);  // M5/M6
        // ---- M11: mesa de guerra ----
        if (const auto w = json->find("warMap"); w != json->end() && w->is_object()) {
            warMap_.enabled      = w->value("enabled", warMap_.enabled);
            warMap_.usePrismaUI  = w->value("usePrismaUI", warMap_.usePrismaUI);
            warMap_.view         = w->value("view", warMap_.view);
            warMap_.pauseGame    = w->value("pauseGame", warMap_.pauseGame);
            warMap_.pageSize     = std::clamp(w->value("pageSize", warMap_.pageSize), 1u, 7u);
            warMap_.nearbyRadius = w->value("nearbyRadius", warMap_.nearbyRadius);
            warMap_.maxTargets   = w->value("maxTargets", warMap_.maxTargets);
        }
        // ---- fim M11 ----
        // ---- M14: guarnição ----
        if (const auto g = json->find("garrison"); g != json->end() && g->is_object()) {
            garrison_.enabled       = g->value("enabled", garrison_.enabled);
            garrison_.maxActors     = g->value("maxActors", garrison_.maxActors);
            garrison_.spawnPerFrame = std::max<std::uint32_t>(1, g->value("spawnPerFrame", garrison_.spawnPerFrame));
            garrison_.spawnRadius   = g->value("spawnRadius", garrison_.spawnRadius);
            garrison_.spreadRadius  = g->value("spreadRadius", garrison_.spreadRadius);
        }
        // ---- fim M14 ----
        LoadAiAndDiplomacy(*json);   // M16/M17
        LoadLawAndStability(*json);  // M25/M26
        LoadArmyAndBattle(*json);    // M20/M21
    }

    // ---- M5/M6: blocos "conquest" e "seat" ------------------------------------------------------
    void Config::LoadConquestAndSeat(const nlohmann::json& a_json)
    {
        // "Plugin.esp|0xID" no mesmo formato de Discovery::KeyOf; texto sem '|' = id de território do JSON.
        const auto normalizeKey = [](const std::string& a_text) {
            const auto ref = FormRef::Parse(a_text);
            return ref ? MakeId(std::format("{}|0x{:06X}", ref->plugin, ref->localId)) : MakeId(a_text);
        };
        const auto readOffset = [](const nlohmann::json& a_obj, const char* a_name, RE::NiPoint3& a_out) {
            const auto v = a_obj.value(a_name, std::vector<float>{});
            if (v.size() == 3) {
                a_out = { v[0], v[1], v[2] };
            } else if (!v.empty()) {
                logger::warn("settings.json: '{}' precisa de 3 números [direita, frente, cima].", a_name);
            }
        };

        if (const auto c = a_json.find("conquest"); c != a_json.end() && c->is_object()) {
            conquest_.enabled                 = c->value("enabled", conquest_.enabled);
            conquest_.requirePlayerInLocation = c->value("requirePlayerInLocation", conquest_.requirePlayerInLocation);
            conquest_.onlyFromNeutral         = c->value("onlyFromNeutral", conquest_.onlyFromNeutral);
            conquest_.defaultStyle            = c->value("defaultStyle", std::string{});
            if (c->contains("skipCivilWarForts")) {  // M16: escalar do usuário vence o dos Compat/*.json
                conquest_.skipCivilWarForts = c->value("skipCivilWarForts", false);
                userSkipCwConquest_         = true;
            }
            for (const auto& v : c->value("exclude", std::vector<std::string>{})) {
                conquest_.exclude.insert(normalizeKey(v));
            }
            for (auto v : c->value("excludePlugins", std::vector<std::string>{})) {
                std::ranges::transform(v, v.begin(), [](char ch) { return static_cast<char>(std::tolower(ch)); });
                conquest_.excludePlugins.push_back(std::move(v));
            }
            if (auto color = c->value("defaultColor", std::string{}); !color.empty()) {
                std::string_view text{ color };
                if (text.starts_with('#')) {
                    text.remove_prefix(1);
                }
                std::uint32_t value{};
                const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
                if (ec == std::errc{} && ptr == text.data() + text.size() && text.size() == 6) {
                    conquest_.defaultColor = value;
                } else {
                    logger::warn("settings.json: conquest.defaultColor inválida (use \"#RRGGBB\").");
                }
            }
        }
        if (const auto s = a_json.find("seat"); s != a_json.end() && s->is_object()) {
            seat_.askBeforeFounding = s->value("askBeforeFounding", seat_.askBeforeFounding);
            // "auto" (ou ausente) = o DLL escolhe um objeto vanilla adequado (ver Seat::Load).
            if (const auto b = s->value("chest", "auto"s); b != "auto") {
                seat_.chest = FormRef::Parse(b);
                if (!seat_.chest) {
                    logger::warn("settings.json: seat.chest inválido; usando escolha automática.");
                }
            }
            if (const auto b = s->value("warTable", "auto"s); b != "auto") {
                seat_.warTable = FormRef::Parse(b);
                if (!seat_.warTable) {
                    logger::warn("settings.json: seat.warTable inválido; usando escolha automática.");
                }
            }
            readOffset(*s, "chestOffset", seat_.chestOffset);
            readOffset(*s, "warTableOffset", seat_.warTableOffset);
        }
    }
    // ---- fim M5/M6 ------------------------------------------------------------------------------

    // ---- M16/M17: blocos "ai" e "diplomacy" -----------------------------------------------------
    void Config::LoadAiAndDiplomacy(const nlohmann::json& a_json)
    {
        if (const auto a = a_json.find("ai"); a != a_json.end() && a->is_object()) {
            ai_.enabled                   = a->value("enabled", ai_.enabled);
            ai_.tickHours                 = std::max(1u, a->value("tickHours", ai_.tickHours));
            ai_.maxEvaluationsPerTick     = a->value("maxEvaluationsPerTick", ai_.maxEvaluationsPerTick);
            ai_.maxActiveAttacks          = a->value("maxActiveAttacks", ai_.maxActiveAttacks);
            ai_.maxAttacksPerFaction      = a->value("maxAttacksPerFaction", ai_.maxAttacksPerFaction);
            ai_.cooldownHours             = a->value("cooldownHours", ai_.cooldownHours);
            ai_.attackBase                = a->value("attackBase", ai_.attackBase);
            ai_.goldPerStrength           = std::max(0.01, a->value("goldPerStrength", ai_.goldPerStrength));
            ai_.minGoldPerAttack          = a->value("minGoldPerAttack", ai_.minGoldPerAttack);
            ai_.maxGoldPerAttack          = a->value("maxGoldPerAttack", ai_.maxGoldPerAttack);
            ai_.minAdvantage              = a->value("minAdvantage", ai_.minAdvantage);
            ai_.randomness                = std::clamp(a->value("randomness", ai_.randomness), 0.0, 0.9);
            ai_.hoursPer10kUnits          = a->value("hoursPer10000Units", ai_.hoursPer10kUnits);
            ai_.minTravelHours            = std::max(1u, a->value("minTravelHours", ai_.minTravelHours));
            ai_.maxTravelHours            = std::max(ai_.minTravelHours, a->value("maxTravelHours", ai_.maxTravelHours));
            ai_.nearPlayerRadius          = a->value("nearPlayerRadius", ai_.nearPlayerRadius);
            ai_.attackPlayer              = a->value("attackPlayer", ai_.attackPlayer);
            ai_.expandIntoNeutral         = a->value("expandIntoNeutral", ai_.expandIntoNeutral);
            ai_.neutralDefenseMultiplier  = a->value("neutralDefenseMultiplier", ai_.neutralDefenseMultiplier);
            ai_.declareWarRatio           = a->value("declareWarRatio", ai_.declareWarRatio);
            ai_.respectConquestExclusions = a->value("respectConquestExclusions", ai_.respectConquestExclusions);
            if (a->contains("skipCivilWarForts")) {  // escalar do usuário vence o dos Compat/*.json
                ai_.skipCivilWarForts = a->value("skipCivilWarForts", false);
                userSkipCwAi_         = true;
            }
            if (const auto types = a->find("expandTypes"); types != a->end() && types->is_array()) {
                ai_.expandTypes.clear();
                for (const auto& v : *types) {
                    const auto type = v.is_string() ? ParseTerritoryType(v.get<std::string>()) : std::nullopt;
                    if (type) {
                        ai_.expandTypes.push_back(*type);
                    } else {
                        logger::warn("settings.json: ai.expandTypes com tipo inválido ignorado.");
                    }
                }
            }
            if (const auto d = a->find("defenseByType"); d != a->end() && d->is_object()) {
                for (const auto& [key, value] : d->items()) {
                    const auto type =
                        key == "unknown" ? std::optional{ TerritoryType::kUnknown } : ParseTerritoryType(key);
                    if (type && value.is_number()) {
                        ai_.defenseByType[*type] = value.get<double>();
                    } else {
                        logger::warn("settings.json: ai.defenseByType '{}' inválido; ignorado.", key);
                    }
                }
            }
        }
        if (const auto d = a_json.find("diplomacy"); d != a_json.end() && d->is_object()) {
            diplomacy_.truceHours               = d->value("truceHours", diplomacy_.truceHours);
            diplomacy_.peaceAcceptRatio         = d->value("peaceAcceptRatio", diplomacy_.peaceAcceptRatio);
            diplomacy_.allianceAcceptRatio      = d->value("allianceAcceptRatio", diplomacy_.allianceAcceptRatio);
            diplomacy_.allianceNeedsCommonEnemy = d->value("allianceNeedsCommonEnemy", diplomacy_.allianceNeedsCommonEnemy);
            diplomacy_.warOnConquest            = d->value("warOnConquest", diplomacy_.warOnConquest);
        }
    }

    // ---- M25/M26: blocos "law" e "stability" ------------------------------------------------------
    void Config::LoadLawAndStability(const nlohmann::json& a_json)
    {
        if (const auto l = a_json.find("law"); l != a_json.end() && l->is_object()) {
            law_.touchCrimeFaction        = l->value("touchCrimeFaction", law_.touchCrimeFaction);
            law_.resetBounty              = l->value("resetBounty", law_.resetBounty);
            law_.captureFraction          = std::clamp(l->value("captureFraction", law_.captureFraction), 0.0, 1.0);
            law_.baseCapacityPerTerritory = l->value("baseCapacityPerTerritory", law_.baseCapacityPerTerritory);
            law_.ransomCostPerPrisoner    = std::max(0.0, l->value("ransomCostPerPrisoner", law_.ransomCostPerPrisoner));
            law_.debugCaptureCount        = std::max(1u, l->value("debugCaptureCount", law_.debugCaptureCount));
        }
        if (const auto s = a_json.find("stability"); s != a_json.end() && s->is_object()) {
            stability_.enabled            = s->value("enabled", stability_.enabled);
            stability_.baseline           = std::clamp(s->value("baseline", stability_.baseline), 0.0, 100.0);
            stability_.popToStrength      = std::max(0.0, s->value("popToStrength", stability_.popToStrength));
            stability_.garrisonPenalty    = std::max(0.0, s->value("garrisonPenalty", stability_.garrisonPenalty));
            stability_.deficitPenalty     = std::max(0.0, s->value("deficitPenalty", stability_.deficitPenalty));
            stability_.rebellionThreshold = std::clamp(s->value("rebellionThreshold", stability_.rebellionThreshold),
                                                        0.0, 100.0);
            stability_.graceHours         = s->value("graceHours", stability_.graceHours);
            stability_.chancePerHour      = std::clamp(s->value("chancePerHour", stability_.chancePerHour), 0.0, 1.0);
            stability_.rebelFaction       = s->value("rebelFaction", stability_.rebelFaction);
            stability_.unrestDecayPerHour = std::max(0.0, s->value("unrestDecayPerHour", stability_.unrestDecayPerHour));
        }
    }
    // ---- fim M25/M26 --------------------------------------------------------------------------------
    // ---- M20/M21: blocos "army" e "battle" -------------------------------------------------------
    void Config::LoadArmyAndBattle(const nlohmann::json& a_json)
    {
        if (const auto a = a_json.find("army"); a != a_json.end() && a->is_object()) {
            army_.enabled           = a->value("enabled", army_.enabled);
            army_.desertPerHour     = a->value("desertPerHour", army_.desertPerHour);
            army_.deficitGraceHours = a->value("deficitGraceHours", army_.deficitGraceHours);
            army_.aiUpkeep          = a->value("aiUpkeep", army_.aiUpkeep);
            army_.hoursPer10kUnits  = a->value("hoursPer10000Units", army_.hoursPer10kUnits);
            army_.minTravelHours    = std::max(1u, a->value("minTravelHours", army_.minTravelHours));
            army_.maxTravelHours    = std::max(army_.minTravelHours, a->value("maxTravelHours", army_.maxTravelHours));
            army_.maxOfficers       = a->value("maxOfficers", army_.maxOfficers);
            army_.officerBonusEach  = a->value("officerBonusEach", army_.officerBonusEach);
            army_.maxEscort         = a->value("maxEscort", army_.maxEscort);
        }
        if (const auto b = a_json.find("battle"); b != a_json.end() && b->is_object()) {
            battle_.realTime         = b->value("realTime", battle_.realTime);
            battle_.maxActors        = b->value("maxActors", battle_.maxActors);
            battle_.spawnPerFrame    = std::max<std::uint32_t>(1, b->value("spawnPerFrame", battle_.spawnPerFrame));
            battle_.radius           = b->value("radius", battle_.radius);
            battle_.sideDistance     = b->value("sideDistance", battle_.sideDistance);
            battle_.formationSpacing = b->value("formationSpacing", battle_.formationSpacing);
            battle_.randomness       = std::clamp(b->value("randomness", battle_.randomness), 0.0, 0.9);
            battle_.maxFrameMs       = b->value("maxFrameMs", battle_.maxFrameMs);
        }
    }
    // ---- fim M20/M21 ------------------------------------------------------------------------------

    // Compat/*.json (ADR 0042, formato em patches/README.md): exclusões condicionais trazidas por patches.
    // Ativo se QUALQUER plugin de "whenLoaded" estiver carregado (vazio = sempre). Listas somam às do
    // settings.json; escalares só valem se o settings.json não os definiu.
    void Config::LoadCompat()
    {
        ScopedTimer           timer{ "Config::LoadCompat" };
        std::error_code       ec;
        std::vector<fs::path> files;
        for (const auto& entry : fs::directory_iterator(kRoot / "Compat", ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                files.push_back(entry.path());
            }
        }
        std::sort(files.begin(), files.end());
        const auto dh           = RE::TESDataHandler::GetSingleton();
        const auto normalizeRef = [](const std::string& a_text) -> std::optional<Id> {
            const auto ref = FormRef::Parse(a_text);
            return ref ? std::optional{ MakeId(std::format("{}|0x{:06X}", ref->plugin, ref->localId)) } : std::nullopt;
        };
        const auto strings = [](const nlohmann::json& a_obj, const char* a_key) {
            std::vector<std::string> out;
            if (const auto it = a_obj.find(a_key); it != a_obj.end() && it->is_array()) {
                for (const auto& v : *it) {
                    if (v.is_string()) {
                        out.push_back(v.get<std::string>());
                    }
                }
            }
            return out;
        };

        for (const auto& f : files) {
            const auto json = ReadJson(f);
            if (!json || !json->is_object()) {
                continue;
            }
            const auto name = json->value("name", f.stem().string());
            try {
                const auto  when = strings(*json, "whenLoaded");
                std::string trigger;
                for (const auto& plugin : when) {
                    if (dh && (dh->LookupLoadedModByName(plugin) || dh->LookupLoadedLightModByName(plugin))) {
                        trigger = plugin;
                        break;
                    }
                }
                if (!when.empty() && trigger.empty()) {
                    logger::info("Compat: {} inativo (nenhum plugin de whenLoaded carregado).", name);
                    continue;
                }
                std::size_t count = 0;
                if (const auto d = json->find("discovery"); d != json->end() && d->is_object()) {
                    for (const auto& v : strings(*d, "exclude")) {
                        if (const auto id = normalizeRef(v)) {
                            discovery_.exclude.insert(*id);
                            ++count;
                        } else {
                            logger::warn("Compat {}: discovery.exclude '{}' inválido (use \"Plugin.esp|0xID\").", name, v);
                        }
                    }
                }
                if (const auto c = json->find("conquest"); c != json->end() && c->is_object()) {
                    for (const auto& v : strings(*c, "exclude")) {
                        const auto id = normalizeRef(v);
                        conquest_.exclude.insert(id ? *id : MakeId(v));  // sem '|' = id de território
                        ++count;
                    }
                    // "excludePlugins" (nome do settings.json, M5) e "excludeFromPlugins" (nome do ADR 0042).
                    for (const auto* key : { "excludePlugins", "excludeFromPlugins" }) {
                        for (auto v : strings(*c, key)) {
                            std::ranges::transform(v, v.begin(), [](char ch) { return static_cast<char>(std::tolower(ch)); });
                            if (std::find(conquest_.excludePlugins.begin(), conquest_.excludePlugins.end(), v) ==
                                conquest_.excludePlugins.end()) {
                                conquest_.excludePlugins.push_back(std::move(v));
                                ++count;
                            }
                        }
                    }
                    if (!userSkipCwConquest_ && c->contains("skipCivilWarForts")) {
                        conquest_.skipCivilWarForts = c->value("skipCivilWarForts", false);
                    }
                }
                if (const auto a = json->find("ai"); a != json->end() && a->is_object()) {
                    if (!userSkipCwAi_ && a->contains("skipCivilWarForts")) {
                        ai_.skipCivilWarForts = a->value("skipCivilWarForts", false);
                    }
                }
                logger::info("Compat: {} ativo{} ({} exclusão(ões)).", name,
                             trigger.empty() ? ""s : std::format(" por '{}'", trigger), count);
            } catch (const std::exception& e) {
                logger::error("Compat: erro em {}: {}", f.filename().string(), e.what());
            }
        }
        logger::info("Compat: {} arquivo(s); conquista com {} exclusão(ões) e {} plugin(s) excluído(s); fortes da "
                     "Guerra Civil {} (conquista) / {} (IA).",
                     files.size(), conquest_.exclude.size(), conquest_.excludePlugins.size(),
                     conquest_.skipCivilWarForts ? "excluídos" : "liberados",
                     ai_.skipCivilWarForts ? "excluídos" : "liberados");
    }

    void Config::SetDefaultOwner(Id a_territory, Id a_owner)
    {
        if (const auto it = index_.find(a_territory); it != index_.end()) {
            auto& t = territories_[it->second];
            if (t.defaultOwner == kNeutral) {
                t.defaultOwner = a_owner;
            }
        }
    }
    // ---- fim M16/M17 ------------------------------------------------------------------------------

    void Config::LoadTerritoryFile(const fs::path& a_file)
    {
        const auto json = ReadJson(a_file);
        if (!json || !json->is_object()) {
            return;
        }

        // Estilos (heráldicas) declarados por este arquivo.
        if (const auto it = json->find("styles"); it != json->end() && it->is_array()) {
            for (const auto& s : *it) {
                if (!s.is_string()) {
                    continue;
                }
                const auto key = s.get<std::string>();
                const auto id  = MakeId(key);
                if (std::find(styles_.begin(), styles_.end(), id) == styles_.end()) {
                    styles_.push_back(id);
                    RegisterName(id, key);
                }
            }
        }

        const auto list = json->find("territories");
        if (list == json->end() || !list->is_array()) {
            return;
        }

        for (const auto& jt : *list) {
            Territory t;
            t.key = jt.value("id", std::string{});
            if (t.key.empty()) {
                logger::warn("{}: território sem 'id', ignorado.", a_file.filename().string());
                continue;
            }
            t.id   = MakeId(t.key);
            t.name = jt.contains("name") ? I18n::Pick(jt["name"], t.key) : t.key;  // "texto" ou {"en":..,"pt-br":..}
            if (index_.contains(t.id)) {
                logger::warn("Território duplicado '{}', ignorado.", t.key);
                continue;
            }
            RegisterName(t.id, t.key);

            if (const auto loc = jt.value("location", std::string{}); !loc.empty()) {
                t.locationRef = FormRef::Parse(loc);
                if (!t.locationRef) {
                    logger::warn("'{}': location inválido (use \"Plugin.esp|0xID\").", t.key);
                }
            }
            if (const auto type = jt.value("type", std::string{}); !type.empty()) {
                t.type = ParseTerritoryType(type).value_or(TerritoryType::kUnknown);
            }

            // Facção dona em jogo novo; validada em State::ResetToDefaults (facções carregam depois).
            if (const auto owner = jt.value("defaultOwner", std::string{}); !owner.empty()) {
                t.defaultOwner = MakeId(owner);
            }

            if (const auto b = jt.find("banners"); b != jt.end() && b->is_object()) {
                for (const auto& [styleKey, refs] : b->items()) {
                    const auto styleId = MakeId(styleKey);
                    if (std::find(styles_.begin(), styles_.end(), styleId) == styles_.end()) {
                        logger::warn("'{}': estilo '{}' não declarado em \"styles\"; ignorado.", t.key, styleKey);
                        continue;
                    }
                    BannerSet set;
                    set.style = styleId;
                    // Cada item: "Plugin.esp|0xID" (ref pré-colocada) ou
                    // { "place": "Plugin.esp|0xID", "offset": [direita, frente, cima] } (criado em runtime).
                    for (const auto& r : refs) {
                        if (r.is_string()) {
                            if (const auto parsed = FormRef::Parse(r.get<std::string>())) {
                                set.refs.push_back(*parsed);
                                continue;
                            }
                        } else if (r.is_object() && r.contains("place")) {
                            const auto base = FormRef::Parse(r.value("place", std::string{}));
                            const auto off  = r.value("offset", std::vector<float>{});
                            if (base && (off.empty() || off.size() == 3)) {
                                PlacedSpec spec;
                                spec.key  = MakeId(std::format("{}/{}/{}", t.key, styleKey, set.placed.size()));
                                spec.base = *base;
                                if (off.size() == 3) {
                                    spec.offset = { off[0], off[1], off[2] };
                                }
                                set.placed.push_back(std::move(spec));
                                continue;
                            }
                        }
                        logger::warn("'{}/{}': item inválido (use \"Plugin.esp|0xID\" ou {{\"place\": ..., \"offset\": [x,y,z]}}).",
                                     t.key, styleKey);
                    }
                    t.banners.push_back(std::move(set));
                }
            }

            index_.emplace(t.id, territories_.size());
            territories_.push_back(std::move(t));
        }
    }

    void Config::MergeDiscovered()
    {
        ScopedTimer timer{ "Config::MergeDiscovered" };

        // Preenche o que o JSON não disse com o que o jogo sabe do local (tipo, Hold, posição, nome).
        const auto fill = [](Territory& a_t, const Discovery::Found& a_f) {
            a_t.location = a_f.location;
            if (a_t.type == TerritoryType::kUnknown) {
                a_t.type = a_f.type;
            }
            if (a_t.province == kNeutral && a_f.hold) {
                a_t.province = MakeId(Discovery::KeyOf(a_f.hold));
                const auto n = a_f.hold->GetFullName();
                a_t.provinceName = n && *n ? n : Discovery::KeyOf(a_f.hold);
            }
            if (!a_t.hasPosition && a_f.hasPosition) {
                a_t.hasPosition = true;
                a_t.position    = a_f.position;
                a_t.worldspace  = a_f.worldspace;
            }
            if (a_t.name == a_t.key) {  // JSON sem "name": usa o nome do jogo (já no idioma do jogo)
                if (const auto n = a_f.location->GetFullName(); n && *n) {
                    a_t.name = n;
                }
            }
        };

        // 1. Territórios do JSON ligados a um local do jogo.
        for (std::size_t i = 0; i < territories_.size(); ++i) {
            auto& t = territories_[i];
            if (!t.locationRef) {
                continue;
            }
            const auto loc = t.locationRef->Resolve<RE::BGSLocation>();
            if (!loc) {
                logger::warn("'{}': location {}|0x{:X} não encontrado (plugin ausente?).", t.key, t.locationRef->plugin,
                             t.locationRef->localId);
                continue;
            }
            if (!byLocation_.try_emplace(loc, i).second) {
                logger::warn("'{}': location já usado por outro território; ignorado.", t.key);
                continue;
            }
            fill(t, Discovery::Describe(loc));
        }

        // 2. Locais do jogo ainda sem território.
        if (discovery_.enabled) {
            for (const auto& f : Discovery::Scan(discovery_)) {
                if (byLocation_.contains(f.location)) {
                    continue;
                }
                Territory t;
                t.key        = f.key;
                t.id         = MakeId(t.key);
                t.name       = t.key;
                t.discovered = true;
                if (index_.contains(t.id)) {
                    continue;
                }
                fill(t, f);
                RegisterName(t.id, t.key);
                index_.emplace(t.id, territories_.size());
                byLocation_.emplace(f.location, territories_.size());
                territories_.push_back(std::move(t));
            }
        }

        ComputeNeighbors();
    }

    // k vizinhos mais próximos no mesmo worldspace. O(n²) uma vez no load (n ~ centenas).
    void Config::ComputeNeighbors()
    {
        ScopedTimer timer{ "Config::ComputeNeighbors" };
        const auto  k = discovery_.neighbors;
        std::vector<std::pair<float, Id>> dist;
        for (auto& t : territories_) {
            t.neighbors.clear();
            if (!t.hasPosition || k == 0) {
                continue;
            }
            dist.clear();
            for (const auto& o : territories_) {
                if (&o != &t && o.hasPosition && o.worldspace == t.worldspace) {
                    dist.emplace_back(t.position.GetSquaredDistance(o.position), o.id);
                }
            }
            const auto n = std::min(k, dist.size());
            std::partial_sort(dist.begin(), dist.begin() + static_cast<std::ptrdiff_t>(n), dist.end());
            for (std::size_t i = 0; i < n; ++i) {
                t.neighbors.push_back(dist[i].second);
            }
        }
    }

    const Territory* Config::FindByLocation(const RE::BGSLocation* a_location) const
    {
        const auto it = byLocation_.find(a_location);
        return it == byLocation_.end() ? nullptr : &territories_[it->second];
    }

    const Territory* Config::Find(Id a_territory) const
    {
        if (const auto it = index_.find(a_territory); it != index_.end()) {
            return &territories_[it->second];
        }
        std::shared_lock l{ dynamicLock_ };  // M8: claims
        const auto       it = dynamicIndex_.find(a_territory);
        return it == dynamicIndex_.end() ? nullptr : it->second;
    }

    // ---- M8: territórios dinâmicos ----------------------------------------------------------------

    const Territory* Config::AddDynamic(Territory a_t)
    {
        if (index_.contains(a_t.id)) {
            logger::error("Config::AddDynamic: '{}' colide com um território do JSON/descoberta.", a_t.key);
            return nullptr;
        }
        // Província e vizinhos a partir dos territórios estáticos (O(n) uma vez por claim).
        if (a_t.hasPosition) {
            std::vector<std::pair<float, const Territory*>> dist;
            for (const auto& o : territories_) {
                if (o.hasPosition && o.worldspace == a_t.worldspace) {
                    dist.emplace_back(a_t.position.GetSquaredDistance(o.position), &o);
                }
            }
            const auto n = std::min(discovery_.neighbors, dist.size());
            std::partial_sort(dist.begin(), dist.begin() + static_cast<std::ptrdiff_t>(n), dist.end());
            a_t.neighbors.clear();
            for (std::size_t i = 0; i < n; ++i) {
                a_t.neighbors.push_back(dist[i].second->id);
            }
            for (std::size_t i = 0; i < n && a_t.province == kNeutral; ++i) {
                if (dist[i].second->province != kNeutral) {
                    a_t.province     = dist[i].second->province;
                    a_t.provinceName = dist[i].second->provinceName;
                }
            }
        }
        std::unique_lock l{ dynamicLock_ };
        if (const auto it = dynamicIndex_.find(a_t.id); it != dynamicIndex_.end()) {
            return it->second;  // já registrado (ex.: load repetido)
        }
        auto& stored = dynamic_.emplace_back(std::move(a_t));
        dynamicIndex_.emplace(stored.id, &stored);
        return &stored;
    }

    void Config::ClearDynamic()
    {
        std::unique_lock l{ dynamicLock_ };
        dynamicIndex_.clear();
        dynamic_.clear();
    }

    std::optional<std::size_t> Config::IndexOf(Id a_territory) const
    {
        const auto it = index_.find(a_territory);
        return it == index_.end() ? std::nullopt : std::optional{ it->second };
    }

    std::string_view Config::NameOf(Id a_id) const
    {
        if (a_id == kNeutral) {
            return "neutral";
        }
        const auto it = names_.find(a_id);
        return it == names_.end() ? "?" : std::string_view{ it->second };
    }
}
