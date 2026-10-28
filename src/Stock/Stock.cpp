#include "Stock/Stock.h"

#include "Api/Events.h"
#include "Core/I18n.h"
#include "Core/Timer.h"

namespace AoS
{
    namespace
    {
        const fs::path kFile = "Data/SKSE/Plugins/AgeOfSkyrim/Resources.json";

        std::optional<RE::FormType> ParseFormType(std::string_view a_name)
        {
            static const std::unordered_map<std::string_view, RE::FormType> kTypes{
                { "Weapon", RE::FormType::Weapon },           { "Armor", RE::FormType::Armor },
                { "Ammo", RE::FormType::Ammo },               { "Misc", RE::FormType::Misc },
                { "Ingredient", RE::FormType::Ingredient },   { "AlchemyItem", RE::FormType::AlchemyItem },
                { "Book", RE::FormType::Book },               { "SoulGem", RE::FormType::SoulGem },
                { "Scroll", RE::FormType::Scroll },           { "KeyMaster", RE::FormType::KeyMaster },
            };
            const auto it = kTypes.find(a_name);
            return it == kTypes.end() ? std::nullopt : std::optional{ it->second };
        }

        // Ao abrir um ContainerMenu guarda o alvo; ao fechar, reconta se for um container observado.
        class ContainerSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
        {
        public:
            static ContainerSink* Get()
            {
                static ContainerSink instance;
                return std::addressof(instance);
            }

            RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event,
                                                  RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
            {
                if (!a_event || (!Stock::Get().AnyObserved() && target_ == 0) ||
                    a_event->menuName != RE::ContainerMenu::MENU_NAME) {
                    return RE::BSEventNotifyControl::kContinue;
                }
                if (a_event->opening) {
                    target_ = 0;
                    if (const auto ref = RE::TESObjectREFR::LookupByHandle(RE::ContainerMenu::GetTargetRefHandle());
                        ref && Stock::Get().IsObserved(ref->GetFormID())) {
                        target_ = ref->GetFormID();
                    }
                } else if (target_ != 0) {
                    const auto id = std::exchange(target_, 0);
                    SKSE::GetTaskInterface()->AddTask([id] { Stock::Get().Recount(id); });
                }
                return RE::BSEventNotifyControl::kContinue;
            }

        private:
            RE::FormID target_{ 0 };
        };
    }

    Stock& Stock::Get() noexcept
    {
        static Stock instance;
        return instance;
    }

    void Stock::Load()
    {
        ScopedTimer timer{ "Stock::Load" };
        categories_.clear();
        byForm_.clear();
        byKeyword_.clear();
        byFormType_.clear();
        cache_.clear();

        std::ifstream in(kFile);
        if (!in) {
            logger::warn("Stock: {} não encontrado; estoque desativado.", kFile.string());
            return;
        }
        nlohmann::json json;
        try {
            json = nlohmann::json::parse(in, nullptr, true, /*ignore_comments*/ true);
        } catch (const std::exception& e) {
            logger::error("Stock: JSON inválido em Resources.json: {}", e.what());
            return;
        }

        // A ordem das categorias é a prioridade: um item com keywords de duas categorias fica na primeira.
        for (const auto& jc : json.value("categories", nlohmann::json::array())) {
            const auto key = jc.value("id", std::string{});
            if (key.empty()) {
                continue;
            }
            const auto idx = static_cast<std::int16_t>(categories_.size());
            categories_.push_back({ key, jc.contains("name") ? I18n::Pick(jc["name"], key) : key });

            for (const auto& f : jc.value("forms", std::vector<std::string>{})) {
                const auto ref  = FormRef::Parse(f);
                const auto form = ref ? ref->Resolve<RE::TESForm>() : nullptr;
                if (form) {
                    byForm_.try_emplace(form->GetFormID(), idx);
                } else {
                    logger::warn("Stock '{}': form '{}' não encontrado.", key, f);
                }
            }
            for (const auto& k : jc.value("keywords", std::vector<std::string>{})) {
                const auto ref = FormRef::Parse(k);
                const auto kw  = ref ? ref->Resolve<RE::BGSKeyword>() : nullptr;
                if (kw) {
                    byKeyword_.try_emplace(kw->GetFormID(), idx);
                    logger::info("Stock '{}': keyword {} = '{}'", key, k, kw->GetFormEditorID());  // conferir IDs
                } else {
                    logger::warn("Stock '{}': keyword '{}' não encontrada (ou não é keyword).", key, k);
                }
            }
            for (const auto& t : jc.value("formTypes", std::vector<std::string>{})) {
                if (const auto type = ParseFormType(t)) {
                    byFormType_.try_emplace(*type, idx);
                } else {
                    logger::warn("Stock '{}': tipo de form '{}' desconhecido.", key, t);
                }
            }
        }
        logger::info("Stock: {} categoria(s), {} keyword(s), {} form(s), {} tipo(s).", categories_.size(),
                     byKeyword_.size(), byForm_.size(), byFormType_.size());

        static bool registered = false;
        if (!registered) {
            if (const auto ui = RE::UI::GetSingleton()) {
                ui->AddEventSink<RE::MenuOpenCloseEvent>(ContainerSink::Get());
                registered = true;
            }
        }
    }

    std::int16_t Stock::Classify(RE::TESBoundObject* a_object)
    {
        const auto id = a_object->GetFormID();
        if (const auto it = cache_.find(id); it != cache_.end()) {
            return it->second;
        }

        std::int16_t result = kNone;
        if (const auto it = byForm_.find(id); it != byForm_.end()) {
            result = it->second;
        } else {
            if (const auto kwForm = a_object->As<RE::BGSKeywordForm>()) {
                for (std::uint32_t i = 0; i < kwForm->numKeywords; ++i) {
                    const auto kw = kwForm->keywords[i];
                    if (!kw) {
                        continue;
                    }
                    if (const auto k = byKeyword_.find(kw->GetFormID());
                        k != byKeyword_.end() && (result == kNone || k->second < result)) {
                        result = k->second;
                    }
                }
            }
            if (result == kNone) {
                if (const auto t = byFormType_.find(a_object->GetFormType()); t != byFormType_.end()) {
                    result = t->second;
                }
            }
        }
        cache_.emplace(id, result);
        return result;
    }

    Stock::Totals Stock::CountByCategory(RE::TESObjectREFR* a_container)
    {
        ScopedTimer timer{ "Stock::CountByCategory" };
        Totals totals;
        totals.perCategory.assign(categories_.size(), 0);
        if (!a_container) {
            return totals;
        }
        for (const auto& [object, count] : a_container->GetInventoryCounts()) {
            if (!object || count <= 0) {
                continue;
            }
            const auto cat = Classify(object);
            if (cat == kNone) {
                totals.unclassified += count;
            } else {
                totals.perCategory[static_cast<std::size_t>(cat)] += count;
            }
        }
        return totals;
    }

    void Stock::Observe(RE::FormID a_container) { observed_.insert(a_container); }

    void Stock::Unobserve(RE::FormID a_container)
    {
        observed_.erase(a_container);
        last_.erase(a_container);
    }

    bool Stock::IsObserved(RE::FormID a_container) const { return observed_.contains(a_container); }

    std::optional<Stock::Totals> Stock::LastTotals(RE::FormID a_container) const
    {
        const auto it = last_.find(a_container);
        return it == last_.end() ? std::nullopt : std::optional{ it->second };
    }

    void Stock::Recount(RE::FormID a_container)
    {
        const auto ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_container);
        if (!ref) {
            return;
        }
        last_[a_container] = CountByCategory(ref);
        logger::info("Stock: container 0x{:08X} recontado.", a_container);
        Api::Events::StockChanged(a_container);  // TODO(M11): atualizar o mapa tático

    }

    void Stock::ReportPlayerInventory()
    {
        if (categories_.empty()) {
            I18n::NotifyTr("stock.disabled");
            return;
        }
        const auto totals = CountByCategory(RE::PlayerCharacter::GetSingleton());
        I18n::NotifyTr("stock.title");
        for (std::size_t i = 0; i < categories_.size(); ++i) {
            logger::info("Stock: {} ({}) = {}", categories_[i].key, categories_[i].name, totals.perCategory[i]);
            if (totals.perCategory[i] > 0) {
                I18n::NotifyTr("stock.line", categories_[i].name, totals.perCategory[i]);
            }
        }
        logger::info("Stock: sem categoria = {}", totals.unclassified);
        I18n::NotifyTr("stock.unclassified", totals.unclassified);
    }
}
