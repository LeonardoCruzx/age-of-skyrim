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
        default:                      return "unknown";
        }
    }

    std::optional<TerritoryType> ParseTerritoryType(std::string_view a_text) noexcept
    {
        for (const auto t : { TerritoryType::kFort, TerritoryType::kTower, TerritoryType::kCamp,
                              TerritoryType::kVillage, TerritoryType::kCity, TerritoryType::kClaim }) {
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
            debug_.enabled = dbg->value("enabled", false);
            debug_.hotkey         = dbg->value("hotkey", 0x44);
            debug_.selfTestHotkey = dbg->value("selfTestHotkey", 0x57);
            debug_.stockHotkey    = dbg->value("stockHotkey", 0x42);
            debug_.claimHotkey    = dbg->value("claimHotkey", 0x3D);
            // ---- M12/M13: economia e rotas ----
            debug_.economyHotkey = dbg->value("economyHotkey", 0x40);
            debug_.routeHotkey   = dbg->value("routeHotkey", 0x41);
            debug_.economyHours  = dbg->value("economyHours", 24u);
            // ---- fim M12/M13 ----
            debug_.conquestHotkey = dbg->value("conquestHotkey", 0x3E);
            debug_.realmHotkey         = dbg->value("realmHotkey", debug_.realmHotkey);
            debug_.realmHoursHotkey    = dbg->value("realmHoursHotkey", debug_.realmHoursHotkey);
            debug_.realmDoctrineHotkey = dbg->value("realmDoctrineHotkey", debug_.realmDoctrineHotkey);
            debug_.realmConquerHotkey  = dbg->value("realmConquerHotkey", debug_.realmConquerHotkey);
            debug_.realmDebugHours     = dbg->value("realmDebugHours", debug_.realmDebugHours);
            debug_.realmConquerCount   = dbg->value("realmConquerCount", debug_.realmConquerCount);
            debug_.warMapHotkey   = dbg->value("warMapHotkey", 0x49);  // M11
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
        }
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
