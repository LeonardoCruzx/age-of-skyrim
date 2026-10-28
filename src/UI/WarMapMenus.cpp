#include "Core/Factions.h"
#include "Core/I18n.h"
#include "Core/State.h"
#include "Sim/Routes.h"
#include "Stock/Stock.h"
#include "UI/Prompt.h"
#include "UI/WarMap.h"
#include "UI/WarMapViews.h"
#include "World/Seat.h"

// M11: fallback sem PrismaUI (ADR 0053). Cadeia de MessageBox vanilla (UI/Prompt), cada resposta abre a próxima:
//   principal -> lista paginada (próximos / meus / alvos / rotas) -> painel do território -> ação.
// Criar rota: "Iniciar rota aqui" num território seu guarda a ponta A; no painel de outro território seu aparece
// "Criar rota até aqui". Tudo na thread principal (o Prompt já devolve a escolha numa task).
namespace AoS::WarMapMenus
{
    namespace
    {
        enum class ListKind : std::uint8_t
        {
            kNearby,
            kMine,
            kTargets,
        };

        Id routeStart_{ kNeutral };  // ponta A escolhida para a próxima rota (só nesta sessão de menus)

        void ShowMain();
        void ShowList(ListKind a_kind, std::size_t a_page);
        void ShowInfo(Id a_territory, ListKind a_back, std::size_t a_backPage);
        void ShowRoutes(std::size_t a_page);

        [[nodiscard]] Id PlayerFaction()
        {
            const auto f = Factions::Get().PlayerFaction();
            return f ? f->id : kNeutral;
        }

        [[nodiscard]] std::string NameOf(Id a_territory)
        {
            const auto* t = Config::Get().Find(a_territory);
            return t ? t->name : "?"s;
        }

        [[nodiscard]] std::string TypeName(TerritoryType a_type)
        {
            return I18n::Tr(std::format("warmap.type.{}", ToString(a_type)));
        }

        // Ponto de referência dos "próximos": o jogador em exterior; em interior, a posição do território da sede.
        [[nodiscard]] std::optional<std::pair<RE::NiPoint3, RE::FormID>> Reference()
        {
            if (const auto pc = RE::PlayerCharacter::GetSingleton()) {
                const auto cell = pc->GetParentCell();
                if (const auto ws = pc->GetWorldspace(); cell && !cell->IsInteriorCell() && ws) {
                    return std::pair{ pc->GetPosition(), ws->GetFormID() };
                }
            }
            if (const auto* t = Config::Get().Find(Seat::Get().SeatTerritory()); t && t->hasPosition) {
                return std::pair{ t->position, t->worldspace };
            }
            return std::nullopt;
        }

        [[nodiscard]] std::vector<Id> Collect(ListKind a_kind)
        {
            std::vector<Id> out;
            const auto&     state = State::Get();
            if (a_kind == ListKind::kTargets) {
                return WarMap::Get().Targets();
            }
            if (a_kind == ListKind::kMine) {
                const auto player = PlayerFaction();
                std::vector<std::pair<std::string, Id>> named;
                Config::Get().ForEachTerritory([&](const Territory& t) {
                    if (player != kNeutral && state.Owner(t.id) == player) {
                        named.emplace_back(t.name, t.id);
                    }
                });
                std::ranges::sort(named);
                for (const auto& [_, id] : named) {
                    out.push_back(id);
                }
                return out;
            }
            const auto ref = Reference();
            if (!ref) {
                return out;
            }
            const auto radius = Config::Get().WarMap().nearbyRadius;
            std::vector<std::pair<float, Id>> near;
            Config::Get().ForEachTerritory([&](const Territory& t) {
                if (!t.hasPosition || t.worldspace != ref->second) {
                    return;
                }
                const float dx = t.position.x - ref->first.x;
                const float dy = t.position.y - ref->first.y;
                const float d  = std::sqrt(dx * dx + dy * dy);
                if (radius <= 0.0f || d <= radius) {
                    near.emplace_back(d, t.id);
                }
            });
            std::ranges::sort(near);
            for (const auto& [_, id] : near) {
                out.push_back(id);
            }
            return out;
        }

        void Ask(const std::string& a_body, const std::vector<std::string>& a_buttons,
                 std::function<void(std::size_t)> a_onChoice)
        {
            if (!Prompt::Ask(a_body, a_buttons, std::move(a_onChoice))) {
                I18n::NotifyTr("warmap.unavailable");
            }
        }

        void Notify(const WarMap::ActionResult& a_result) { I18n::Notify(a_result.text); }

        // ---- telas ----

        void ShowMain()
        {
            const auto  player = PlayerFaction();
            std::size_t mine   = 0;
            std::size_t total  = 0;
            Config::Get().ForEachTerritory([&](const Territory& t) {
                ++total;
                if (player != kNeutral && State::Get().Owner(t.id) == player) {
                    ++mine;
                }
            });
            std::string body = I18n::Tr("warmap.menu.title", NameOf(Seat::Get().SeatTerritory()));
            body += '\n';
            body += I18n::Tr("warmap.menu.summary", mine, total, Routes::Get().List().size(), Routes::Get().MaxRoutes(),
                             WarMap::Get().Targets().size());
            if (const auto totals = Seat::Get().StockTotals()) {
                const auto& cats = Stock::Get().Categories();
                for (std::size_t i = 0; i < cats.size() && i < totals->perCategory.size(); ++i) {
                    if (totals->perCategory[i] > 0) {
                        body += '\n';
                        body += I18n::Tr("stock.line", cats[i].name, totals->perCategory[i]);
                    }
                }
            }
            if (routeStart_ != kNeutral) {
                body += '\n';
                body += I18n::Tr("warmap.menu.routePending", NameOf(routeStart_));
            }
            Ask(body,
                { I18n::Tr("warmap.menu.nearby"), I18n::Tr("warmap.menu.mine"), I18n::Tr("warmap.menu.targets"),
                  I18n::Tr("warmap.menu.routes"), I18n::Tr("warmap.menu.close") },
                [](std::size_t a_choice) {
                    switch (a_choice) {
                    case 0: ShowList(ListKind::kNearby, 0); break;
                    case 1: ShowList(ListKind::kMine, 0); break;
                    case 2: ShowList(ListKind::kTargets, 0); break;
                    case 3: ShowRoutes(0); break;
                    default: break;  // fechar
                    }
                });
        }

        void ShowList(ListKind a_kind, std::size_t a_page)
        {
            const auto ids   = Collect(a_kind);
            const auto size  = static_cast<std::size_t>(Config::Get().WarMap().pageSize);
            const auto pages = std::max<std::size_t>(1, (ids.size() + size - 1) / size);
            const auto page  = std::min(a_page, pages - 1);
            const auto first = page * size;
            const auto last  = std::min(ids.size(), first + size);

            std::vector<std::string> buttons;
            std::vector<Id>          shown;
            for (auto i = first; i < last; ++i) {
                const auto* t = Config::Get().Find(ids[i]);
                if (!t) {
                    continue;
                }
                buttons.push_back(I18n::Tr("warmap.menu.entry", t->name, TypeName(t->type)));
                shown.push_back(t->id);
            }
            const bool more = last < ids.size();
            if (more) {
                buttons.push_back(I18n::Tr("warmap.menu.next"));
            }
            buttons.push_back(I18n::Tr("warmap.menu.back"));

            const std::string_view titleKey = a_kind == ListKind::kNearby ? "warmap.menu.nearby" :
                                              a_kind == ListKind::kMine   ? "warmap.menu.mine" :
                                                                            "warmap.menu.targets";
            std::string body = I18n::Tr("warmap.menu.page", I18n::Tr(titleKey), page + 1, pages);
            if (ids.empty()) {
                body += '\n';
                body += I18n::Tr("warmap.menu.empty");
            }
            Ask(body, buttons, [a_kind, page, shown, more](std::size_t a_choice) {
                if (a_choice < shown.size()) {
                    ShowInfo(shown[a_choice], a_kind, page);
                } else if (more && a_choice == shown.size()) {
                    ShowList(a_kind, page + 1);
                } else {
                    ShowMain();
                }
            });
        }

        void ShowInfo(Id a_territory, ListKind a_back, std::size_t a_backPage)
        {
            const auto* t = Config::Get().Find(a_territory);
            if (!t) {
                ShowList(a_back, a_backPage);
                return;
            }
            const auto player = PlayerFaction();
            const auto owner  = State::Get().Owner(t->id);
            const bool mine   = player != kNeutral && owner == player;
            const bool target = WarMap::Get().IsTarget(t->id);

            std::string body = t->name;
            body += '\n' + I18n::Tr("warmap.info.type", TypeName(t->type));
            body += '\n' + I18n::Tr("warmap.info.province", t->provinceName.empty() ? "-"s : t->provinceName);
            body += '\n' + I18n::Tr("warmap.info.owner", Factions::Get().NameOf(owner));
            std::size_t routes = 0;
            double      gold   = 0.0;
            for (const auto& r : Routes::Get().List()) {
                if (r.a == t->id || r.b == t->id) {
                    ++routes;
                    if (const auto s = Routes::Get().StatusOf(r.id); s && s->active && !s->blocked) {
                        gold += s->goldPerHour;
                    }
                }
            }
            body += '\n' + I18n::Tr("warmap.info.routes", routes, std::format("{:.1f}", gold));
            if (t->id == Seat::Get().SeatTerritory()) {
                body += '\n' + I18n::Tr("warmap.info.seat");
            }
            if (target) {
                body += '\n' + I18n::Tr("warmap.info.target");
            }
            for (const auto& [label, value] : WarMap::Get().LayersOf(*t)) {
                body += '\n' + I18n::Tr("warmap.info.layer", label,
                                        value.is_string() ? value.get<std::string>() : value.dump());
            }

            // Botões variam conforme o território: guardo a ação de cada um.
            enum class Act : std::uint8_t { kRouteTo, kRouteFrom, kTarget, kBack };
            std::vector<std::string> buttons;
            std::vector<Act>         acts;
            if (mine && routeStart_ != kNeutral && routeStart_ != t->id) {
                buttons.push_back(I18n::Tr("warmap.menu.routeTo", NameOf(routeStart_)));
                acts.push_back(Act::kRouteTo);
            }
            if (mine && routeStart_ != t->id) {
                buttons.push_back(I18n::Tr("warmap.menu.routeFrom"));
                acts.push_back(Act::kRouteFrom);
            }
            if (!mine) {
                buttons.push_back(I18n::Tr(target ? "warmap.menu.untarget" : "warmap.menu.target"));
                acts.push_back(Act::kTarget);
            }
            buttons.push_back(I18n::Tr("warmap.menu.back"));
            acts.push_back(Act::kBack);

            Ask(body, buttons, [acts, id = t->id, target, a_back, a_backPage](std::size_t a_choice) {
                const auto act = a_choice < acts.size() ? acts[a_choice] : Act::kBack;
                switch (act) {
                case Act::kRouteTo:
                    Notify(WarMap::Get().CreateRoute(routeStart_, id));
                    routeStart_ = kNeutral;
                    ShowInfo(id, a_back, a_backPage);
                    break;
                case Act::kRouteFrom:
                    routeStart_ = id;
                    I18n::NotifyTr("warmap.menu.pickSecond", NameOf(id));
                    ShowList(ListKind::kMine, 0);
                    break;
                case Act::kTarget:
                    Notify(WarMap::Get().SetTarget(id, !target));
                    ShowInfo(id, a_back, a_backPage);
                    break;
                default:
                    ShowList(a_back, a_backPage);
                    break;
                }
            });
        }

        void ShowRoute(Id a_route, std::size_t a_backPage)
        {
            const auto routes = Routes::Get().List();
            const auto it     = std::ranges::find(routes, a_route, &Routes::Route::id);
            const auto status = Routes::Get().StatusOf(a_route);
            if (it == routes.end() || !status) {
                ShowRoutes(a_backPage);
                return;
            }
            std::string body = I18n::Tr("warmap.menu.routeLine", NameOf(it->a), NameOf(it->b));
            body += '\n' + I18n::Tr("warmap.info.owner", Factions::Get().NameOf(status->owner));
            body += '\n' + I18n::Tr(!status->active ? "warmap.route.inactive" :
                                    status->blocked ? "warmap.route.blocked" :
                                                      "warmap.route.active");
            if (status->blocked) {
                body += '\n' + I18n::Tr("warmap.route.blockedBy", NameOf(status->blocker));
            }
            body += '\n' + I18n::Tr("warmap.route.stats", static_cast<int>(status->distance / 70.0f),
                                    std::format("{:.1f}", status->goldPerHour));
            Ask(body, { I18n::Tr("warmap.menu.removeRoute"), I18n::Tr("warmap.menu.back") },
                [a_route, a_backPage](std::size_t a_choice) {
                    if (a_choice == 0) {
                        Notify(WarMap::Get().RemoveRoute(a_route));
                    }
                    ShowRoutes(a_backPage);
                });
        }

        void ShowRoutes(std::size_t a_page)
        {
            const auto routes = Routes::Get().List();
            const auto size   = static_cast<std::size_t>(Config::Get().WarMap().pageSize);
            const auto pages  = std::max<std::size_t>(1, (routes.size() + size - 1) / size);
            const auto page   = std::min(a_page, pages - 1);
            const auto first  = page * size;
            const auto last   = std::min(routes.size(), first + size);

            std::vector<std::string> buttons;
            std::vector<Id>          shown;
            for (auto i = first; i < last; ++i) {
                buttons.push_back(I18n::Tr("warmap.menu.routeLine", NameOf(routes[i].a), NameOf(routes[i].b)));
                shown.push_back(routes[i].id);
            }
            const bool more = last < routes.size();
            if (more) {
                buttons.push_back(I18n::Tr("warmap.menu.next"));
            }
            buttons.push_back(I18n::Tr("warmap.menu.back"));
            std::string body = I18n::Tr("warmap.menu.page", I18n::Tr("warmap.menu.routes"), page + 1, pages);
            if (routes.empty()) {
                body += '\n';
                body += I18n::Tr("warmap.menu.empty");
            }
            Ask(body, buttons, [shown, more, page](std::size_t a_choice) {
                if (a_choice < shown.size()) {
                    ShowRoute(shown[a_choice], page);
                } else if (more && a_choice == shown.size()) {
                    ShowRoutes(page + 1);
                } else {
                    ShowMain();
                }
            });
        }
    }

    bool Open()
    {
        if (!RE::MessageDataFactoryManager::GetSingleton()) {
            return false;
        }
        logger::info("Mapa tático: menus vanilla abertos.");
        ShowMain();
        return true;
    }
}
