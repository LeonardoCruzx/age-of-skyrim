#include "World/Discovery.h"

#include "Core/Timer.h"

#include <array>
#include <map>

namespace AoS::Discovery
{
    namespace
    {
        // Keywords vanilla de tipo de local (Skyrim.esm). Resolvidas por EditorID: BGSKeyword guarda o
        // EditorID mesmo sem po3 Tweaks. Ordem = prioridade quando um local tem mais de uma.
        struct TypeKeyword
        {
            std::string_view editorId;
            TerritoryType    type;
        };

        constexpr std::array kTypeKeywords{
            TypeKeyword{ "LocTypeCity", TerritoryType::kCity },
            TypeKeyword{ "LocTypeTown", TerritoryType::kVillage },
            TypeKeyword{ "LocTypeSettlement", TerritoryType::kVillage },
            TypeKeyword{ "LocTypeOrcStronghold", TerritoryType::kVillage },
            TypeKeyword{ "LocTypeMilitaryFort", TerritoryType::kFort },
            TypeKeyword{ "LocTypeMilitaryCamp", TerritoryType::kCamp },
            TypeKeyword{ "LocTypeBanditCamp", TerritoryType::kCamp },
            TypeKeyword{ "LocTypeForswornCamp", TerritoryType::kCamp },
            TypeKeyword{ "LocTypeGiantCamp", TerritoryType::kCamp },
            // M29: casas do jogador (vanilla e de outros mods que seguem a convenção). As que ficam dentro de
            // uma cidade saem sozinhas pela remoção de sub-locais abaixo (a cidade já é candidata do tipo "city").
            TypeKeyword{ "LocTypePlayerHouse", TerritoryType::kHouse },
        };
        constexpr std::string_view kHoldKeyword = "LocTypeHold";

        struct Keywords
        {
            std::array<const RE::BGSKeyword*, kTypeKeywords.size()> types{};
            const RE::BGSKeyword*                                   hold{ nullptr };
        };

        // Uma passada pelas keywords carregadas (custo único no load).
        const Keywords& GetKeywords()
        {
            static const Keywords kw = [] {
                Keywords k;
                const auto dh = RE::TESDataHandler::GetSingleton();
                if (!dh) {
                    return k;
                }
                for (const auto keyword : dh->GetFormArray<RE::BGSKeyword>()) {
                    const auto ed = keyword ? keyword->GetFormEditorID() : nullptr;
                    if (!ed || !*ed) {
                        continue;
                    }
                    for (std::size_t i = 0; i < kTypeKeywords.size(); ++i) {
                        if (_stricmp(ed, kTypeKeywords[i].editorId.data()) == 0) {
                            k.types[i] = keyword;
                        }
                    }
                    if (_stricmp(ed, kHoldKeyword.data()) == 0) {
                        k.hold = keyword;
                    }
                }
                for (std::size_t i = 0; i < kTypeKeywords.size(); ++i) {
                    if (!k.types[i]) {
                        logger::warn("Descoberta: keyword '{}' não encontrada.", kTypeKeywords[i].editorId);
                    }
                }
                if (!k.hold) {
                    logger::warn("Descoberta: keyword '{}' não encontrada; províncias ficarão vazias.", kHoldKeyword);
                }
                return k;
            }();
            return kw;
        }

        TerritoryType Classify(const RE::BGSLocation* a_location, const Keywords& a_kw)
        {
            for (std::size_t i = 0; i < kTypeKeywords.size(); ++i) {
                if (a_kw.types[i] && a_location->HasKeyword(a_kw.types[i])) {
                    return kTypeKeywords[i].type;
                }
            }
            return TerritoryType::kUnknown;
        }
    }

    bool IsMilitaryFort(const RE::BGSLocation* a_location)
    {
        if (!a_location) {
            return false;
        }
        const auto& kw = GetKeywords();
        for (std::size_t i = 0; i < kTypeKeywords.size(); ++i) {
            if (kTypeKeywords[i].editorId == "LocTypeMilitaryFort") {
                return kw.types[i] && a_location->HasKeyword(kw.types[i]);
            }
        }
        return false;
    }

    std::string KeyOf(const RE::TESForm* a_form)
    {
        const auto file = a_form ? a_form->GetFile(0) : nullptr;
        if (!file) {
            return std::format("?|0x{:08X}", a_form ? a_form->GetFormID() : 0);
        }
        return std::format("{}|0x{:06X}", file->GetFilename(), a_form->GetLocalFormID());
    }

    Found Describe(RE::BGSLocation* a_location)
    {
        const auto& kw = GetKeywords();
        Found       f;
        f.location = a_location;
        f.key      = KeyOf(a_location);
        f.type     = Classify(a_location, kw);

        if (kw.hold) {
            auto parent = a_location->parentLoc;
            for (int depth = 0; parent && depth < 16; ++depth, parent = parent->parentLoc) {
                if (parent->HasKeyword(kw.hold)) {
                    f.hold = parent;
                    break;
                }
            }
        }
        if (const auto marker = a_location->worldLocMarker.get()) {
            f.hasPosition = true;
            f.position    = marker->GetPosition();
            if (const auto ws = marker->GetWorldspace()) {
                f.worldspace = ws->GetFormID();
            }
        }
        return f;
    }

    std::vector<Found> Scan(const DiscoverySettings& a_settings)
    {
        ScopedTimer t{ "Discovery::Scan" };
        std::vector<Found> found;
        const auto         dh = RE::TESDataHandler::GetSingleton();
        if (!dh) {
            return found;
        }
        const auto& kw = GetKeywords();

        std::unordered_set<RE::BGSLocation*> candidates;
        for (const auto loc : dh->GetFormArray<RE::BGSLocation>()) {
            if (!loc) {
                continue;
            }
            const auto type = Classify(loc, kw);
            if (type == TerritoryType::kUnknown ||
                std::find(a_settings.types.begin(), a_settings.types.end(), type) == a_settings.types.end()) {
                continue;
            }
            if (a_settings.exclude.contains(MakeId(KeyOf(loc)))) {
                continue;
            }
            candidates.insert(loc);
        }

        for (const auto loc : candidates) {
            bool nested = false;  // sub-local de outro candidato: o de fora representa o território
            auto parent = loc->parentLoc;
            for (int depth = 0; parent && depth < 16 && !nested; ++depth, parent = parent->parentLoc) {
                nested = candidates.contains(parent);
            }
            if (!nested) {
                found.push_back(Describe(loc));
            }
        }
        // Ordem determinística (o unordered_set não é): por chave.
        std::sort(found.begin(), found.end(), [](const Found& a, const Found& b) { return a.key < b.key; });
        logger::info("Descoberta: {} local(is) candidato(s), {} território(s) após remover sub-locais.",
                     candidates.size(), found.size());
        return found;
    }

    void LogSummary()
    {
        const auto& cfg = Config::Get();
        std::map<TerritoryType, std::size_t> byType;
        std::size_t discovered = 0, withProvince = 0, withPosition = 0, withNeighbors = 0;
        for (const auto& t : cfg.Territories()) {
            if (!t.location) {
                continue;
            }
            ++discovered;
            ++byType[t.type];
            withProvince += t.province != kNeutral;
            withPosition += t.hasPosition;
            withNeighbors += !t.neighbors.empty();
        }
        logger::info("[descoberta] {} território(s) do jogo: {} cidade(s), {} vila(s), {} forte(s), {} torre(s), "
                     "{} acampamento(s), {} casa(s).",
                     discovered, byType[TerritoryType::kCity], byType[TerritoryType::kVillage],
                     byType[TerritoryType::kFort], byType[TerritoryType::kTower], byType[TerritoryType::kCamp],
                     byType[TerritoryType::kHouse]);

        // Mínimos de um Skyrim vanilla (5 capitais grandes, dezenas de fortes e acampamentos).
        const auto check = [](bool a_ok, std::string_view a_what) {
            if (a_ok) {
                logger::info("[descoberta] PASS  {}", a_what);
            } else {
                logger::error("[descoberta] FAIL  {}", a_what);
            }
        };
        const auto pct = [&](std::size_t n) { return discovered ? 100.0 * static_cast<double>(n) / static_cast<double>(discovered) : 0.0; };
        check(byType[TerritoryType::kCity] >= 5, std::format("cidades >= 5 ({})", byType[TerritoryType::kCity]));
        check(byType[TerritoryType::kVillage] >= 10, std::format("vilas >= 10 ({})", byType[TerritoryType::kVillage]));
        check(byType[TerritoryType::kFort] >= 10, std::format("fortes >= 10 ({})", byType[TerritoryType::kFort]));
        check(byType[TerritoryType::kCamp] >= 10, std::format("acampamentos >= 10 ({})", byType[TerritoryType::kCamp]));
        check(pct(withProvince) >= 90.0, std::format("com província >= 90% ({:.0f}%)", pct(withProvince)));
        check(pct(withPosition) >= 90.0, std::format("com posição no mapa >= 90% ({:.0f}%)", pct(withPosition)));
        check(pct(withNeighbors) >= 90.0, std::format("com vizinhos >= 90% ({:.0f}%)", pct(withNeighbors)));
    }

    void WriteDump()
    {
        auto path = SKSE::log::log_directory();
        if (!path) {
            return;
        }
        *path /= "AgeOfSkyrim_territories.json";

        const auto&    cfg = Config::Get();
        nlohmann::json list = nlohmann::json::array();
        for (const auto& t : cfg.Territories()) {
            nlohmann::json j{
                { "id", t.key },
                { "name", t.name },
                { "type", ToString(t.type) },
                { "source", t.discovered ? "game" : "json" },
            };
            if (t.location) {
                j["location"] = KeyOf(t.location);
            }
            if (t.province != kNeutral) {
                j["province"] = t.provinceName;
            }
            if (t.hasPosition) {
                j["position"]   = { t.position.x, t.position.y, t.position.z };
                j["worldspace"] = std::format("0x{:08X}", t.worldspace);
            }
            auto& n = j["neighbors"] = nlohmann::json::array();
            for (const auto id : t.neighbors) {
                n.push_back(cfg.Find(id) ? cfg.Find(id)->key : "?");
            }
            list.push_back(std::move(j));
        }

        std::ofstream out(*path);
        if (!out) {
            logger::warn("Descoberta: não foi possível escrever {}.", path->string());
            return;
        }
        // Nomes vêm do jogo (podem não ser UTF-8 válido): troca bytes inválidos em vez de lançar exceção.
        out << nlohmann::json{ { "territories", std::move(list) } }.dump(2, ' ', false,
                                                                          nlohmann::json::error_handler_t::replace);
        logger::info("Descoberta: lista completa em {}.", path->string());
    }
}
