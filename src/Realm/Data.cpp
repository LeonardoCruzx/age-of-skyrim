#include "Realm/Data.h"

#include "Core/I18n.h"
#include "Core/Timer.h"

namespace AoS::Realm
{
    namespace
    {
        const fs::path kRoot = "Data/SKSE/Plugins/AgeOfSkyrim";

        // Todos os *.json de uma pasta, em ordem alfabética (o último a declarar um id vence).
        template <class F>
        void ForEachJson(const fs::path& a_dir, F&& a_fn)
        {
            std::error_code       ec;
            std::vector<fs::path> files;
            for (const auto& entry : fs::directory_iterator(a_dir, ec)) {
                if (entry.is_regular_file() && entry.path().extension() == ".json") {
                    files.push_back(entry.path());
                }
            }
            std::sort(files.begin(), files.end());
            for (const auto& f : files) {
                std::ifstream in(f);
                if (!in) {
                    continue;
                }
                try {
                    const auto json = nlohmann::json::parse(in, nullptr, true, /*ignore_comments*/ true);
                    if (json.is_object()) {
                        a_fn(json, f.filename().string());
                    }
                } catch (const std::exception& e) {
                    logger::error("JSON inválido em {}: {}", f.string(), e.what());
                }
            }
        }

        // "<campo>Key": chave de tradução (en.json/pt-br.json); senão "<campo>": texto ou { "en", "pt-br" }.
        std::string Text(const nlohmann::json& a_json, const std::string& a_field, std::string_view a_fallback)
        {
            if (const auto k = a_json.find(a_field + "Key"); k != a_json.end() && k->is_string()) {
                return std::string(I18n::Raw(k->get<std::string>()));
            }
            if (const auto f = a_json.find(a_field); f != a_json.end()) {
                return I18n::Pick(*f, a_fallback);
            }
            return std::string(a_fallback);
        }

        Id OptionalId(const nlohmann::json& a_json, const char* a_field)
        {
            const auto text = a_json.value(a_field, std::string{});
            return text.empty() ? kNeutral : MakeId(text);
        }

        // Insere ou substitui (mesmo id) num vetor de definições.
        template <class T>
        void Upsert(std::vector<T>& a_list, T&& a_item, std::string_view a_what)
        {
            for (auto& existing : a_list) {
                if (existing.id == a_item.id) {
                    logger::info("Reino: {} '{}' redefinido por um arquivo posterior.", a_what, a_item.key);
                    existing = std::move(a_item);
                    return;
                }
            }
            a_list.push_back(std::move(a_item));
        }

        template <class T>
        const T* FindById(const std::vector<T>& a_list, Id a_id)
        {
            for (const auto& item : a_list) {
                if (item.id == a_id) {
                    return &item;
                }
            }
            return nullptr;
        }
    }

    Data& Data::Get() noexcept
    {
        static Data instance;
        return instance;
    }

    void Data::Load()
    {
        ScopedTimer timer{ "Realm::Data::Load" };
        doctrines_.clear();
        offices_.clear();
        titles_.clear();
        governor_        = {};
        population_      = {};
        defaultDoctrine_ = kNeutral;
        provinceSize_.clear();

        ForEachJson(kRoot / "Doctrines", [&](const nlohmann::json& a_json, const std::string& a_file) {
            LoadDoctrineFile(a_json, a_file);
        });
        ForEachJson(kRoot / "Population", [&](const nlohmann::json& a_json, const std::string&) {
            LoadPopulationFile(a_json);
        });
        ForEachJson(kRoot / "Titles", [&](const nlohmann::json& a_json, const std::string& a_file) {
            LoadTitleFile(a_json, a_file);
        });

        // Títulos em ordem de rank; desbloqueios acumulados (cada título herda os anteriores).
        std::stable_sort(titles_.begin(), titles_.end(), [](const Title& a, const Title& b) { return a.rank < b.rank; });
        for (std::size_t i = 1; i < titles_.size(); ++i) {
            auto merged = titles_[i - 1].unlocks;
            for (const auto& [k, v] : titles_[i].unlocks) {
                merged[k] = v;
            }
            titles_[i].unlocks = std::move(merged);
        }

        // Referências cruzadas: título exigido por doutrina/cargo precisa existir.
        for (auto& d : doctrines_) {
            if (d.requiresTitle != kNeutral && !FindTitle(d.requiresTitle)) {
                logger::warn("Doutrina '{}': requiresTitle desconhecido; liberada para todos.", d.key);
                d.requiresTitle = kNeutral;
            }
        }
        for (auto& o : offices_) {
            if (o.requiresTitle != kNeutral && !FindTitle(o.requiresTitle)) {
                logger::warn("Cargo '{}': requiresTitle desconhecido; liberado para todos.", o.key);
                o.requiresTitle = kNeutral;
            }
        }
        if (defaultDoctrine_ == kNeutral || !FindDoctrine(defaultDoctrine_)) {
            if (defaultDoctrine_ != kNeutral) {
                logger::warn("Reino: defaultDoctrine desconhecida; usando a primeira doutrina.");
            }
            defaultDoctrine_ = doctrines_.empty() ? kNeutral : doctrines_.front().id;
        }

        for (const auto& t : Config::Get().Territories()) {
            if (t.province != kNeutral) {
                ++provinceSize_[t.province];
            }
        }

        logger::info("Reino: {} doutrina(s), {} cargo(s), {} título(s), {} província(s); doutrina padrão '{}'.",
                     doctrines_.size(), offices_.size(), titles_.size(), provinceSize_.size(),
                     FindDoctrine(defaultDoctrine_) ? FindDoctrine(defaultDoctrine_)->key : "-"s);
    }

    void Data::LoadDoctrineFile(const nlohmann::json& a_json, std::string_view a_file)
    {
        if (const auto d = a_json.value("defaultDoctrine", std::string{}); !d.empty()) {
            defaultDoctrine_ = MakeId(d);
        }
        if (const auto list = a_json.find("doctrines"); list != a_json.end() && list->is_array()) {
            for (const auto& jd : *list) {
                Doctrine d;
                d.key = jd.value("id", std::string{});
                if (d.key.empty()) {
                    logger::warn("{}: doutrina sem 'id', ignorada.", a_file);
                    continue;
                }
                d.id            = MakeId(d.key);
                d.name          = Text(jd, "name", d.key);
                d.description   = Text(jd, "description", "");
                d.requiresTitle = OptionalId(jd, "requiresTitle");
                if (const auto m = jd.find("modifiers"); m != jd.end()) {
                    ParseModifiers(*m, d.modifiers, d.key);
                }
                // "changeCost": { "gold": 1000, "food": 50 }
                if (const auto c = jd.find("changeCost"); c != jd.end() && c->is_object()) {
                    for (const auto& [res, amount] : c->items()) {
                        if (amount.is_number() && amount.get<double>() > 0.0) {
                            d.changeCost.push_back({ res, static_cast<std::uint32_t>(amount.get<double>()) });
                        }
                    }
                }
                Upsert(doctrines_, std::move(d), "doutrina");
            }
        }
        if (const auto list = a_json.find("offices"); list != a_json.end() && list->is_array()) {
            for (const auto& jo : *list) {
                Office o;
                o.key = jo.value("id", std::string{});
                if (o.key.empty()) {
                    logger::warn("{}: cargo sem 'id', ignorado.", a_file);
                    continue;
                }
                o.id            = MakeId(o.key);
                o.name          = Text(jo, "name", o.key);
                o.requiresTitle = OptionalId(jo, "requiresTitle");
                if (const auto m = jo.find("modifiers"); m != jo.end()) {
                    ParseModifiers(*m, o.modifiers, o.key);
                }
                Upsert(offices_, std::move(o), "cargo");
            }
        }
        if (const auto g = a_json.find("governor"); g != a_json.end() && g->is_object()) {
            governor_.majority = std::clamp(g->value("majority", governor_.majority), 0.0f, 0.99f);
            if (const auto m = g->find("modifiers"); m != g->end()) {
                governor_.modifiers = {};
                ParseModifiers(*m, governor_.modifiers, "governor");
            }
        }
    }

    void Data::LoadPopulationFile(const nlohmann::json& a_json)
    {
        const auto p = a_json.find("population");
        if (p == a_json.end() || !p->is_object()) {
            return;
        }
        auto& def = population_;
        if (const auto init = p->find("initial"); init != p->end() && init->is_object()) {
            for (const auto& [type, value] : init->items()) {
                const auto t = type == "unknown" ? std::optional{ TerritoryType::kUnknown } : ParseTerritoryType(type);
                if (t && value.is_number()) {
                    def.initial[static_cast<std::size_t>(*t)] = std::max(1.0f, value.get<float>());
                } else {
                    logger::warn("Population: tipo '{}' inválido em \"initial\".", type);
                }
            }
        }
        if (const auto terr = p->find("territories"); terr != p->end() && terr->is_object()) {
            for (const auto& [key, value] : terr->items()) {
                if (value.is_number()) {
                    def.overrides[MakeId(key)] = std::max(1.0f, value.get<float>());
                }
            }
        }
        def.capacityFactor   = std::max(1.0f, p->value("capacityFactor", def.capacityFactor));
        def.growthPerDay     = std::max(0.0f, p->value("growthPerDay", def.growthPerDay));
        def.starvationPerDay = std::max(0.0f, p->value("starvationPerDay", def.starvationPerDay));
        def.captureLoss      = std::clamp(p->value("captureLoss", def.captureLoss), 0.0f, 0.9f);
        def.draftReserve     = std::clamp(p->value("draftReserve", def.draftReserve), 0.0f, 1.0f);
        def.manpowerMin      = std::max(0.0f, p->value("manpowerMin", def.manpowerMin));
        def.manpowerMax      = std::max(def.manpowerMin, p->value("manpowerMax", def.manpowerMax));
    }

    void Data::LoadTitleFile(const nlohmann::json& a_json, std::string_view a_file)
    {
        const auto list = a_json.find("titles");
        if (list == a_json.end() || !list->is_array()) {
            return;
        }
        for (const auto& jt : *list) {
            Title t;
            t.key = jt.value("id", std::string{});
            if (t.key.empty()) {
                logger::warn("{}: título sem 'id', ignorado.", a_file);
                continue;
            }
            t.id   = MakeId(t.key);
            t.name       = Text(jt, "name", t.key);
            t.nameFemale = Text(jt, "nameFemale", "");
            t.rank = jt.value("rank", 0);
            if (const auto r = jt.find("requires"); r != jt.end() && r->is_object()) {
                t.needs.territories = r->value("territories", 0u);
                t.needs.provinces   = r->value("provinces", 0u);
                t.needs.cities      = r->value("cities", 0u);
                t.needs.population  = r->value("population", 0u);
            }
            if (const auto m = jt.find("modifiers"); m != jt.end()) {
                ParseModifiers(*m, t.modifiers, t.key);
            }
            if (const auto u = jt.find("unlocks"); u != jt.end() && u->is_object()) {
                for (const auto& [k, v] : u->items()) {
                    if (v.is_number()) {
                        t.unlocks[k] = v.get<float>();
                    } else {
                        logger::warn("Título '{}': desbloqueio '{}' não numérico ignorado.", t.key, k);
                    }
                }
            }
            Upsert(titles_, std::move(t), "título");
        }
    }

    const Doctrine* Data::FindDoctrine(Id a_id) const { return FindById(doctrines_, a_id); }
    const Office*   Data::FindOffice(Id a_id) const { return FindById(offices_, a_id); }
    const Title*    Data::FindTitle(Id a_id) const { return FindById(titles_, a_id); }

    int Data::RankOf(Id a_title) const
    {
        const auto t = FindTitle(a_title);
        return t ? t->rank : -1;
    }

    std::uint32_t Data::ProvinceSize(Id a_province) const
    {
        const auto it = provinceSize_.find(a_province);
        return it == provinceSize_.end() ? 0u : it->second;
    }

    float Data::InitialPopulation(const Territory& a_territory) const
    {
        if (const auto it = population_.overrides.find(a_territory.id); it != population_.overrides.end()) {
            return it->second;
        }
        return population_.initial[static_cast<std::size_t>(a_territory.type)];
    }
}
