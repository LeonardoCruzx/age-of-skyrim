#pragma once

#include "Core/Config.h"

namespace AoS
{
    // Estoque por categoria (comida, ouro, metais...) a partir de ITENS REAIS num container.
    // Regras em Data/SKSE/Plugins/AgeOfSkyrim/Resources.json: por form específico, por keyword vanilla
    // (VendorItemFood...) e por tipo de form. Itens de outros mods entram sozinhos se usam as keywords padrão.
    // Só na thread principal (contagem lê o inventário do jogo).
    class Stock
    {
    public:
        struct Category
        {
            std::string key;   // "food"
            std::string name;  // localizado
        };

        // Quantidade por categoria (índice = posição em Categories()) + itens sem categoria.
        struct Totals
        {
            std::vector<std::int64_t> perCategory;
            std::int64_t              unclassified{ 0 };
        };

        [[nodiscard]] static Stock& Get() noexcept;

        // Chamar em kDataLoaded, depois de Config::Load (precisa dos forms carregados e do idioma).
        // Também registra o sink de ContainerMenu (uma vez).
        void Load();

        [[nodiscard]] const std::vector<Category>& Categories() const noexcept { return categories_; }

        // Uma passada pelo inventário; classificação memorizada por objeto base (O(1) após a 1ª vez).
        [[nodiscard]] Totals CountByCategory(RE::TESObjectREFR* a_container);

        // ---- M15: custo de obras em itens reais ----
        [[nodiscard]] std::optional<std::size_t> CategoryIndex(std::string_view a_key) const;  // "materials" -> índice
        // Remove até a_amount itens da categoria do container (qualquer item classificado nela). Retorna quantos saíram.
        std::int64_t Take(RE::TESObjectREFR* a_container, std::size_t a_category, std::int64_t a_amount);
        // ---- fim M15 ----

        // Containers "observados" (baú da sede, M6): recontados ao fechar o ContainerMenu deles.
        // Sem nenhum observado, o sink sai na primeira linha (custo zero).
        void Observe(RE::FormID a_container);
        void Unobserve(RE::FormID a_container);
        [[nodiscard]] bool                  IsObserved(RE::FormID a_container) const;
        [[nodiscard]] bool                  AnyObserved() const noexcept { return !observed_.empty(); }
        [[nodiscard]] std::optional<Totals> LastTotals(RE::FormID a_container) const;
        void                                Recount(RE::FormID a_container);

        // Debug (hotkey): conta o inventário do jogador por categoria e mostra na tela + log.
        void ReportPlayerInventory();

    private:
        static constexpr std::int16_t kNone = -1;

        [[nodiscard]] std::int16_t Classify(RE::TESBoundObject* a_object);

        std::vector<Category>                          categories_;
        std::unordered_map<RE::FormID, std::int16_t>   byForm_;      // form específico -> categoria
        std::unordered_map<RE::FormID, std::int16_t>   byKeyword_;   // keyword -> categoria
        std::unordered_map<RE::FormType, std::int16_t> byFormType_;  // tipo de form -> categoria (fallback)
        std::unordered_map<RE::FormID, std::int16_t>   cache_;       // objeto base -> categoria (memo)

        std::unordered_set<RE::FormID>         observed_;
        std::unordered_map<RE::FormID, Totals> last_;
    };
}
