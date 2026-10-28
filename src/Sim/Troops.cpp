#include "Sim/Troops.h"

#include "Core/Factions.h"
#include "Core/Timer.h"
#include "Sim/Economy.h"

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

        std::string Lower(std::string_view a_text)
        {
            std::string out{ a_text };
            std::ranges::transform(out, out.begin(), [](char c) { return static_cast<char>(std::tolower(c)); });
            return out;
        }

        // Mesma convenção de Garrison.cpp (ADR 0044): "Plugin.esp|0xID" ou EditorID (precisa de po3 Tweaks).
        RE::TESForm* ResolveForm(const std::string& a_text)
        {
            if (a_text.find('|') != std::string::npos) {
                const auto ref = FormRef::Parse(a_text);
                return ref ? ref->Resolve<RE::TESForm>() : nullptr;
            }
            return a_text.empty() ? nullptr : RE::TESForm::LookupByEditorID(a_text);
        }

        std::vector<std::string> Candidates(const nlohmann::json& a_obj, const char* a_field)
        {
            const auto it = a_obj.find(a_field);
            if (it == a_obj.end()) {
                return {};
            }
            if (it->is_string()) {
                return { it->get<std::string>() };
            }
            return it->is_array() ? it->get<std::vector<std::string>>() : std::vector<std::string>{};
        }

        template <class T>
        T* ResolveFirst(const std::vector<std::string>& a_candidates, RE::FormType a_type, std::string_view a_what,
                        std::string_view a_troop)
        {
            for (const auto& c : a_candidates) {
                const auto form = ResolveForm(c);
                if (form && form->Is(a_type)) {
                    logger::info("Tropas: '{}': {} '{}' -> 0x{:08X} ('{}').", a_troop, a_what, c, form->GetFormID(),
                                 form->GetName());
                    return static_cast<T*>(form);
                }
                logger::info("Tropas: '{}': {} '{}' não encontrado (plugin ausente / EditorID sem po3 Tweaks / tipo "
                             "errado).",
                             a_troop, a_what, c);
            }
            return nullptr;
        }

        bool ArmorMatches(RE::TESForm* a_item, const std::string& a_hint, int a_depth)
        {
            if (!a_item || a_depth > 2) {
                return false;
            }
            if (const auto armo = a_item->As<RE::TESObjectARMO>()) {
                const auto model = armo->worldModels[0].GetModel();
                return model && Lower(model).find(a_hint) != std::string::npos;
            }
            if (const auto lev = a_item->As<RE::TESLevItem>()) {
                for (const auto f : lev->GetContainedForms()) {
                    if (ArmorMatches(f, a_hint, a_depth + 1)) {
                        return true;
                    }
                }
            }
            return false;
        }

        // Mesma escolha automática de Garrison::AutoSoldier (ADR 0044): NPC genérico (não único) do Skyrim.esm,
        // raça jogável, com nome, cujo outfit padrão tem armadura com a dica no caminho do modelo.
        RE::TESNPC* AutoSoldier(const std::string& a_hint)
        {
            const auto dh = RE::TESDataHandler::GetSingleton();
            if (!dh || a_hint.empty()) {
                return nullptr;
            }
            for (const auto npc : dh->GetFormArray<RE::TESNPC>()) {
                const auto file = npc ? npc->GetFile(0) : nullptr;
                if (!file || _stricmp(file->fileName, "Skyrim.esm") != 0 || npc->IsUnique() || !npc->defaultOutfit) {
                    continue;
                }
                const auto name = npc->GetName();
                if (!name || !*name || !npc->race || !npc->race->GetPlayable()) {
                    continue;
                }
                for (const auto item : npc->defaultOutfit->outfitItems) {
                    if (ArmorMatches(item, a_hint, 0)) {
                        return npc;
                    }
                }
            }
            return nullptr;
        }
    }

    Troops& Troops::Get() noexcept
    {
        static Troops instance;
        return instance;
    }

    void Troops::Load()
    {
        ScopedTimer timer{ "Troops::Load" };
        troops_.clear();

        std::vector<fs::path> files{ kRoot / "Troops.json" };
        std::vector<fs::path> extra;
        std::error_code       ec;
        for (const auto& entry : fs::directory_iterator(kRoot / "Troops", ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                extra.push_back(entry.path());
            }
        }
        std::sort(extra.begin(), extra.end());
        files.insert(files.end(), extra.begin(), extra.end());
        for (const auto& f : files) {
            try {
                LoadFile(f);
            } catch (const std::exception& e) {
                logger::error("Tropas: erro em {}: {}", f.filename().string(), e.what());
            }
        }
        logger::info("Tropas: {} tipo(s) de tropa de campo resolvido(s).", troops_.size());
    }

    void Troops::LoadFile(const fs::path& a_file)
    {
        const auto json = ReadJson(a_file);
        if (!json || !json->is_object()) {
            if (a_file.filename() == "Troops.json") {
                logger::warn("Tropas: {} ausente ou inválido; exércitos ficam sem tipos de tropa.", a_file.string());
            }
            return;
        }
        const auto t = json->find("troops");
        if (t == json->end() || !t->is_object()) {
            return;
        }
        for (const auto& [key, value] : t->items()) {
            if (value.is_object()) {
                ResolveTroop(key, value);
            }
        }
    }

    void Troops::ResolveTroop(const std::string& a_key, const nlohmann::json& a_json)
    {
        Troop tr;
        tr.key = a_key;
        for (const auto& c : Candidates(a_json, "actor")) {
            const auto form = ResolveForm(c);
            if (form && (form->Is(RE::FormType::NPC) || form->Is(RE::FormType::LeveledNPC))) {
                tr.actor = static_cast<RE::TESBoundObject*>(form);
                logger::info("Tropas: '{}': ator '{}' -> 0x{:08X} ('{}').", a_key, c, form->GetFormID(),
                             form->GetName());
                break;
            }
            logger::info("Tropas: '{}': ator '{}' não encontrado (plugin ausente / EditorID sem po3 Tweaks / não é "
                         "NPC nem leveled list).",
                         a_key, c);
        }
        if (!tr.actor) {
            const auto hint = Lower(a_json.value("autoArmorHint", std::string{}));
            if (const auto npc = AutoSoldier(hint)) {
                tr.actor = npc;
                logger::info("Tropas: '{}': escolha automática (armadura '{}') -> 0x{:08X} ('{}').", a_key, hint,
                             npc->GetFormID(), npc->GetName());
            }
        }
        if (!tr.actor) {
            logger::warn("Tropas: '{}' sem ator válido; recrutamento/batalha desse tipo ficam indisponíveis.", a_key);
            return;
        }
        tr.outfit = ResolveFirst<RE::BGSOutfit>(Candidates(a_json, "outfit"), RE::FormType::Outfit, "outfit", a_key);
        tr.strength     = a_json.value("strength", tr.strength);
        tr.goldCost      = a_json.value("goldCost", tr.goldCost);
        tr.equipCategory = a_json.value("equipCategory", std::string{});
        tr.equipAmount   = a_json.value("equipAmount", std::int64_t{ 0 });
        tr.trainHours    = std::max<std::uint32_t>(1, a_json.value("trainHours", tr.trainHours));
        if (const auto u = a_json.find("upkeep"); u != a_json.end() && u->is_object()) {
            for (const auto& [resKey, value] : u->items()) {
                const auto id = MakeId(resKey);
                if (!value.is_number() || !Economy::Get().FindResource(id)) {
                    logger::warn("Tropas: '{}': upkeep.{}: recurso desconhecido em Economy.json; ignorado.", a_key,
                                 resKey);
                    continue;
                }
                if (const auto v = value.get<double>(); v > 0.0) {
                    tr.upkeep.emplace_back(id, v);
                }
            }
        }
        troops_[MakeId(a_key)] = std::move(tr);
    }

    const Troops::Troop* Troops::TroopFor(Id a_faction) const
    {
        if (const auto it = troops_.find(a_faction); it != troops_.end()) {
            return &it->second;
        }
        if (const auto it = troops_.find(Factions::Get().StyleOf(a_faction)); it != troops_.end()) {
            return &it->second;
        }
        const auto it = troops_.find(MakeId("default"));
        return it == troops_.end() ? nullptr : &it->second;
    }

    const Troops::Troop* Troops::Find(Id a_troop) const
    {
        const auto it = troops_.find(a_troop);
        return it == troops_.end() ? nullptr : &it->second;
    }

    std::vector<Id> Troops::Keys() const
    {
        std::vector<Id> out;
        out.reserve(troops_.size());
        for (const auto& [id, troop] : troops_) {
            out.push_back(id);
        }
        return out;
    }

    // Igual a Garrison::EnsureOutfit (ADR 0044): outfit SEMPRE aplicado pelo DLL, base vanilla nunca editada.
    void Troops::EnsureOutfit(RE::Actor* a_actor, const Troop& a_troop) const
    {
        const auto npc    = a_actor ? a_actor->GetActorBase() : nullptr;
        const auto outfit = a_troop.outfit ? a_troop.outfit : (npc ? npc->defaultOutfit : nullptr);
        if (!outfit || a_actor->IsDead()) {
            return;
        }
        if (a_troop.outfit && npc && npc->IsDynamicForm()) {
            a_actor->SetDefaultOutfit(outfit, true);
        }
        if (!a_actor->HasOutfitItems(outfit)) {
            a_actor->AddWornOutfit(outfit, true);
        }
    }
}
