#include "UI/DebugMenu.h"

#include "Core/I18n.h"
#include "Debug/DebugActions.h"
#include "UI/Prompt.h"

#include <iterator>
#include <span>

// M30: duas camadas de MessageBox vanilla (UI/Prompt), igual ao padrão já usado em UI/WarMapMenus.cpp:
// categorias -> ações da categoria. Cada botão chama direto a função de Debug/DebugActions (a mesma que a
// tecla dedicada chama); isso aqui é só uma vitrine por nome, nunca duplica lógica de jogo.
namespace AoS::DebugMenu
{
    namespace
    {
        using Prompt::Ask;

        struct Action
        {
            std::string_view labelKey;
            void (*run)();
        };

        struct Category
        {
            std::string_view labelKey;
            std::span<const Action> actions;
        };

        constexpr Action kA[] = { { "debugmenu.action.selftest", DebugActions::SelfTest },
                                  { "debugmenu.action.cycleOwner", DebugActions::CycleOwner } };
        constexpr Action kB[] = { { "debugmenu.action.stock", DebugActions::ReportStock } };
        constexpr Action kC[] = { { "debugmenu.action.claim", DebugActions::TryClaimHere } };
        constexpr Action kD[] = { { "debugmenu.action.conquest", DebugActions::SimulateConquest } };
        constexpr Action kE[] = { { "debugmenu.action.economy", DebugActions::EconomyHours },
                                  { "debugmenu.action.route", DebugActions::CreateRoute } };
        constexpr Action kF[] = { { "debugmenu.action.garrison", DebugActions::GarrisonHere },
                                  { "debugmenu.action.upgrade", DebugActions::UpgradeHere } };
        constexpr Action kG[] = { { "debugmenu.action.realmSummary", DebugActions::RealmSummary },
                                  { "debugmenu.action.realmHours", DebugActions::RealmHours },
                                  { "debugmenu.action.realmDoctrine", DebugActions::RealmDoctrine },
                                  { "debugmenu.action.realmConquer", DebugActions::RealmConquerNearby } };
        constexpr Action kH[] = { { "debugmenu.action.jarl", DebugActions::OfferGovernor } };
        constexpr Action kI[] = { { "debugmenu.action.ai", DebugActions::AiTick },
                                  { "debugmenu.action.diplomacy", DebugActions::DiplomacyCycle } };
        constexpr Action kJ[] = { { "debugmenu.action.lawCapture", DebugActions::LawCapture },
                                  { "debugmenu.action.lawRansom", DebugActions::LawRansom },
                                  { "debugmenu.action.lawExecute", DebugActions::LawExecute } };
        constexpr Action kK[] = { { "debugmenu.action.stability", DebugActions::StabilityHere } };
        constexpr Action kL[] = { { "debugmenu.action.armyRecruit", DebugActions::ArmyRecruit },
                                  { "debugmenu.action.armyList", DebugActions::ArmyList },
                                  { "debugmenu.action.battleTest", DebugActions::BattleTest } };
        constexpr Action kM[] = { { "debugmenu.action.warmap", DebugActions::OpenWarMap } };

        constexpr Category kCategories[] = {
            { "debugmenu.cat.a", kA }, { "debugmenu.cat.b", kB }, { "debugmenu.cat.c", kC },
            { "debugmenu.cat.d", kD }, { "debugmenu.cat.e", kE }, { "debugmenu.cat.f", kF },
            { "debugmenu.cat.g", kG }, { "debugmenu.cat.h", kH }, { "debugmenu.cat.i", kI },
            { "debugmenu.cat.j", kJ }, { "debugmenu.cat.k", kK }, { "debugmenu.cat.l", kL },
            { "debugmenu.cat.m", kM },
        };
        constexpr std::size_t kPageSize = 6;  // mesma referência de UI/WarMapMenus.cpp

        void ShowCategories(std::size_t a_page);
        void ShowActions(std::size_t a_category, std::size_t a_backPage);

        void ShowCategories(std::size_t a_page)
        {
            constexpr auto total = std::size(kCategories);
            const auto     pages = (total + kPageSize - 1) / kPageSize;
            const auto     page  = std::min(a_page, pages - 1);
            const auto     first = page * kPageSize;
            const auto     last  = std::min(total, first + kPageSize);

            std::vector<std::string> buttons;
            std::vector<std::size_t> shown;
            for (auto i = first; i < last; ++i) {
                buttons.push_back(I18n::Tr(kCategories[i].labelKey));
                shown.push_back(i);
            }
            const bool more = last < total;
            if (more) {
                buttons.push_back(I18n::Tr("warmap.menu.next"));
            }
            buttons.push_back(I18n::Tr("warmap.menu.close"));

            const auto body = I18n::Tr("warmap.menu.page", I18n::Tr("debugmenu.title"), page + 1, pages);
            Ask(body, buttons, [page, shown, more](std::size_t a_choice) {
                if (a_choice < shown.size()) {
                    ShowActions(shown[a_choice], page);
                } else if (more && a_choice == shown.size()) {
                    ShowCategories(page + 1);
                }  // senão: fechar
            });
        }

        void ShowActions(std::size_t a_category, std::size_t a_backPage)
        {
            const auto& cat = kCategories[a_category];

            std::vector<std::string> buttons;
            for (const auto& action : cat.actions) {
                buttons.push_back(I18n::Tr(action.labelKey));
            }
            buttons.push_back(I18n::Tr("warmap.menu.back"));

            // Captura o índice, não a referência "cat" (local — não existe mais quando o Prompt chama de volta).
            // Ação escolhida: roda e fecha o menu (várias abrem a própria caixa — sede, governador, mapa tático —
            // e reabrir o menu junto colocaria duas caixas na fila). "Voltar" volta pras categorias.
            Ask(I18n::Tr(cat.labelKey), buttons, [a_category, a_backPage](std::size_t a_choice) {
                const auto& cat = kCategories[a_category];
                if (a_choice < cat.actions.size()) {
                    cat.actions[a_choice].run();
                } else if (a_choice == cat.actions.size()) {
                    ShowCategories(a_backPage);
                }
            });
        }
    }

    void Open() { ShowCategories(0); }
}
