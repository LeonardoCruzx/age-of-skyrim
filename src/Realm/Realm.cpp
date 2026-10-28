#include "Realm/Realm.h"

#include "Api/Events.h"
#include "Banner/BannerController.h"
#include "Core/Factions.h"
#include "Core/State.h"
#include "Core/I18n.h"
#include "Core/Timer.h"
#include "Realm/Domination.h"
#include "Realm/Government.h"
#include "Realm/Jarl.h"
#include "Realm/Population.h"
#include "Realm/Titles.h"

namespace AoS::Realm
{
    namespace
    {
        std::vector<Id> PlayerFactionIds()
        {
            std::vector<Id> out;
            for (const auto& f : Factions::Get().All()) {
                if (f.player) {
                    out.push_back(f.id);
                }
            }
            return out;
        }

        // Nome do título no gênero do jogador (Rei/Rainha) — títulos são da facção do jogador.
        std::string TitleName(const Title* a_title)
        {
            if (!a_title) {
                return I18n::Tr("realm.noTitle");
            }
            const auto player = RE::PlayerCharacter::GetSingleton();
            const auto base   = player ? player->GetActorBase() : nullptr;
            const bool female = base && base->GetSex() == RE::SEX::kFemale;
            return female && !a_title->nameFemale.empty() ? a_title->nameFemale : a_title->name;
        }

        // Anúncios vão para a thread principal (podem vir de qualquer thread via SetTerritoryOwner).
        void AnnounceTitle(Id a_faction, const Title* a_title)
        {
            if (!a_title || !Config::Get().Realm().announceTitles) {
                return;
            }
            SKSE::GetTaskInterface()->AddTask([a_faction, a_title] {
                I18n::NotifyTr("realm.titleUp", Factions::Get().NameOf(a_faction), TitleName(a_title));
            });
        }

        void AnnounceGovernorsLost(const std::vector<Id>& a_provinces)
        {
            for (const auto p : a_provinces) {
                SKSE::GetTaskInterface()->AddTask([p] {
                    I18n::NotifyTr("realm.governorLost", std::string(Config::Get().NameOf(p)));
                });
            }
        }

        // Reavalia governadores e título de qualquer facção; anúncio na tela só para a do jogador. O(territórios).
        void Reevaluate(Id a_faction, bool a_player)
        {
            const auto stats = ComputeStats(a_faction);
            const auto lost  = Government::Get().ValidateGovernors(a_faction, stats.majorityProvinces);
            Jarl::Get().OnGovernorsLost(lost);  // M24: a presença não sobrevive à perda da maioria
            const auto promo      = Titles::Get().Evaluate(a_faction, stats);
            const auto dominated  = Domination::Get().Evaluate(a_faction, stats);  // M27 (ADR 0057)
            if (promo.title && !promo.first) {
                Api::Events::TitleGranted(a_faction, promo.title->id);  // M9: também para facções de IA (M16)
            }
            if (!a_player) {
                return;
            }
            AnnounceGovernorsLost(lost);
            if (promo.title && !promo.first) {
                AnnounceTitle(a_faction, promo.title);
            }
            if (dominated) {
                SKSE::GetTaskInterface()->AddTask(
                    [a_faction] { I18n::NotifyTr("realm.domination", Factions::Get().NameOf(a_faction)); });
            }
        }

        std::optional<Id> EnsurePlayerFaction()
        {
            if (const auto f = Factions::Get().PlayerFaction()) {
                return f->id;
            }
            const auto& styles = Config::Get().Styles();
            if (styles.empty()) {
                logger::warn("Reino (debug): nenhum estilo declarado; não dá para criar a facção do jogador.");
                return std::nullopt;
            }
            return Factions::Get().CreatePlayerFaction(I18n::Tr("realm.debugFactionName"), styles.front(), 0x6A1B9A);
        }

        std::string Slots(Id a_faction, std::string_view a_key)
        {
            const auto v = Titles::Get().Unlock(a_faction, a_key, -1.0f);
            return v < 0.0f ? "-"s : std::format("{:.0f}", v);
        }

        // Hotkeys de debug do reino (settings.json -> debug.realm*). Trabalho na fila da thread principal.
        class DebugSink final : public RE::BSTEventSink<RE::InputEvent*>
        {
        public:
            static DebugSink* Get()
            {
                static DebugSink instance;
                return std::addressof(instance);
            }

            RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>*) override
            {
                if (!a_event) {
                    return RE::BSEventNotifyControl::kContinue;
                }
                const auto& dbg = Config::Get().Debug();
                for (auto e = *a_event; e; e = e->next) {
                    if (e->GetEventType() != RE::INPUT_EVENT_TYPE::kButton) {
                        continue;
                    }
                    const auto b = e->AsButtonEvent();
                    if (!b || !b->IsDown() || b->GetDevice() != RE::INPUT_DEVICE::kKeyboard) {
                        continue;
                    }
                    const auto key = static_cast<int>(b->GetIDCode());
                    void (*action)() = nullptr;
                    if (key == dbg.realmHotkey) {
                        action = ShowSummary;
                    } else if (key == dbg.realmHoursHotkey) {
                        action = DebugSimulateHours;
                    } else if (key == dbg.realmDoctrineHotkey) {
                        action = DebugCycleDoctrine;
                    } else if (key == dbg.realmConquerHotkey) {
                        action = DebugConquerNearby;
                    }
                    if (!action) {
                        continue;
                    }
                    if (const auto ui = RE::UI::GetSingleton(); ui && ui->GameIsPaused()) {
                        continue;
                    }
                    SKSE::GetTaskInterface()->AddTask(action);
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };
    }

    std::recursive_mutex& Mutex() noexcept
    {
        static std::recursive_mutex m;
        return m;
    }

    void Load()
    {
        Data::Get().Load();
        Population::Get().Reset();
        Jarl::Get().Load();  // M24: depois de Conquest::Register (Main.cpp chama Realm::Load só depois)

        static bool registered = false;
        if (Config::Get().Debug().enabled && !registered) {
            if (const auto input = RE::BSInputDeviceManager::GetSingleton()) {
                input->AddEventSink(DebugSink::Get());
                registered  = true;
                const auto& d = Config::Get().Debug();
                logger::info("Reino: hotkeys de debug (resumo 0x{:X}, simular {} h 0x{:X}, doutrina 0x{:X}, conquistar {} 0x{:X}).",
                             d.realmHotkey, d.realmDebugHours, d.realmHoursHotkey, d.realmDoctrineHotkey,
                             d.realmConquerCount, d.realmConquerHotkey);
            }
        }
    }

    void Clear()
    {
        Government::Get().Clear();
        Titles::Get().Clear();
        Population::Get().Reset();
        Domination::Get().Clear();  // M27
        Jarl::Get().Clear();        // M24
    }

    double Clock() { return Government::Get().Clock(); }

    bool HasDominated(Id a_faction) { return Domination::Get().Of(a_faction); }  // M27

    FactionStats ComputeStats(Id a_faction)
    {
        FactionStats s;
        if (a_faction == kNeutral) {
            return s;
        }
        const auto& list = Config::Get().Territories();
        const auto& data = Data::Get();

        std::unordered_map<Id, std::uint32_t> perProvince;  // ~9 Holds
        {
            std::scoped_lock l{ Mutex() };
            for (std::size_t i = 0; i < list.size(); ++i) {
                const auto& t = list[i];
                if (State::Get().Owner(t.id) != a_faction) {
                    continue;
                }
                ++s.territories;
                s.cities += t.type == TerritoryType::kCity ? 1u : 0u;
                s.population += Population::Get().AtIndex(i);
                if (t.province != kNeutral) {
                    ++perProvince[t.province];
                }
            }
        }
        const auto majority = data.Governor().majority;
        for (const auto& [province, owned] : perProvince) {
            if (static_cast<float>(owned) > static_cast<float>(data.ProvinceSize(province)) * majority) {
                s.majorityProvinces.push_back(province);
            }
        }
        return s;
    }

    void OnHoursPassed(float a_hours)
    {
        if (a_hours <= 0.0f) {
            return;
        }
        ScopedTimer timer{ "Realm::OnHoursPassed" };
        Government::Get().Advance(a_hours);
        Population::Get().Tick(a_hours);
        for (const auto f : PlayerFactionIds()) {
            Reevaluate(f, true);  // marcos de população
        }
    }

    void OnTerritoryOwnerChanged(Id a_territory, Id a_oldOwner, Id a_newOwner)
    {
        if (a_oldOwner == a_newOwner) {
            return;
        }
        ScopedTimer timer{ "Realm::OnTerritoryOwnerChanged" };
        Population::Get().OnCaptured(a_territory);
        for (const auto f : { a_oldOwner, a_newOwner }) {
            if (f == kNeutral) {
                continue;
            }
            if (const auto faction = Factions::Get().Find(f)) {
                Reevaluate(f, faction->player);
            }
        }
    }

    // ---- Debug ----------------------------------------------------------------------------------------

    void ShowSummary()
    {
        const auto f = EnsurePlayerFaction();
        if (!f) {
            return;
        }
        Reevaluate(*f, true);  // garante o título inicial (e anuncia se subiu)

        const auto& gov      = Government::Get();
        const auto  stats    = ComputeStats(*f);
        const auto  doctrine = Data::Get().FindDoctrine(gov.DoctrineOf(*f));
        const auto  docName  = doctrine ? doctrine->name : "-"s;
        const auto  wait     = gov.CooldownLeft(*f);
        const auto  mult     = [&](Modifier m) { return std::format("{:.2f}", Modifiers::Get(*f, m)); };

        I18n::NotifyTr("realm.summary.title", Factions::Get().NameOf(*f), TitleName(Titles::Get().Current(*f)));
        if (wait > 0.0f) {
            I18n::NotifyTr("realm.summary.doctrineWait", docName, std::format("{:.0f}", wait));
        } else {
            I18n::NotifyTr("realm.summary.doctrine", docName);
        }
        I18n::NotifyTr("realm.summary.population", std::format("{:.0f}", stats.population), stats.territories,
                       stats.majorityProvinces.size(), stats.cities);
        I18n::NotifyTr("realm.summary.posts", gov.OfficerCount(*f), Slots(*f, "officers"), gov.GovernorCount(*f),
                       Slots(*f, "governors"));
        I18n::NotifyTr("realm.summary.modifiers", mult(Modifier::Production), mult(Modifier::Tax),
                       mult(Modifier::RecruitCost), mult(Modifier::PopulationGrowth), mult(Modifier::Defense));
        logger::info("Reino: relógio {:.1f} h; doutrina '{}' escolhida={}; armyCap {}.", Clock(),
                     doctrine ? doctrine->key : "-"s, gov.HasChosenDoctrine(*f), Slots(*f, "armyCap"));
    }

    void DebugSimulateHours()
    {
        const auto hours = std::max(1.0f, Config::Get().Debug().realmDebugHours);
        const auto start = std::chrono::steady_clock::now();
        OnHoursPassed(hours);
        const std::chrono::duration<double, std::milli> ms = std::chrono::steady_clock::now() - start;
        I18n::NotifyTr("realm.debug.hours", std::format("{:.0f}", hours), std::format("{:.2f}", ms.count()));
        ShowSummary();
    }

    void DebugCycleDoctrine()
    {
        const auto  f    = EnsurePlayerFaction();
        const auto& list = Data::Get().Doctrines();
        if (!f || list.empty()) {
            return;
        }
        auto&      gov = Government::Get();
        const auto cur = gov.DoctrineOf(*f);
        const auto it  = std::ranges::find_if(list, [&](const Doctrine& d) { return d.id == cur; });
        const auto idx = it == list.end() ? 0 : static_cast<std::size_t>(it - list.begin());

        // Tenta as próximas em ordem (pula as travadas por título), para o teste não empacar.
        for (std::size_t step = 1; step <= list.size(); ++step) {
            const auto& next = list[(idx + step) % list.size()];
            const auto  r    = gov.SetDoctrine(*f, next.id);
            logger::info("Reino (debug): doutrina -> '{}': {}.", next.key, ToString(r));
            switch (r) {
            case Government::Result::kOk:
                I18n::NotifyTr("realm.doctrine.changed", next.name);
                return;
            case Government::Result::kCooldown:
                I18n::NotifyTr("realm.doctrine.cooldown", std::format("{:.0f}", gov.CooldownLeft(*f)));
                return;
            case Government::Result::kNoPayment:
                I18n::NotifyTr("realm.doctrine.noPayment", next.name);
                return;
            case Government::Result::kLocked:
                I18n::NotifyTr("realm.doctrine.locked", next.name,
                               TitleName(Data::Get().FindTitle(next.requiresTitle)));
                continue;
            default:
                continue;
            }
        }
    }

    void DebugConquerNearby()
    {
        const auto f = EnsurePlayerFaction();
        if (!f) {
            return;
        }
        const auto  player = RE::PlayerCharacter::GetSingleton();
        const auto  ws     = player ? player->GetWorldspace() : nullptr;
        const auto  pos    = player ? player->GetPosition() : RE::NiPoint3{};
        const auto& list   = Config::Get().Territories();

        // Mais próximos no mesmo worldspace; em interiores (sem worldspace), os primeiros da lista.
        std::vector<std::pair<float, Id>> candidates;
        for (const auto& t : list) {
            if (State::Get().Owner(t.id) == *f) {
                continue;
            }
            if (ws) {
                if (t.hasPosition && t.worldspace == ws->GetFormID()) {
                    candidates.emplace_back(pos.GetSquaredDistance(t.position), t.id);
                }
            } else {
                candidates.emplace_back(static_cast<float>(candidates.size()), t.id);
            }
        }
        const auto n = std::min(candidates.size(), static_cast<std::size_t>(std::max(1, Config::Get().Debug().realmConquerCount)));
        if (n == 0) {
            I18n::NotifyTr("realm.debug.noTerritory");
            return;
        }
        std::partial_sort(candidates.begin(), candidates.begin() + static_cast<std::ptrdiff_t>(n), candidates.end());
        for (std::size_t i = 0; i < n; ++i) {
            SetTerritoryOwner(candidates[i].second, *f);  // passa pelo gancho OnTerritoryOwnerChanged
        }

        // Preenche as vagas que o título permitir (o NPC na sede fica para depois — ADR 0036).
        auto& gov = Government::Get();
        for (const auto& o : Data::Get().Offices()) {
            logger::info("Reino (debug): cargo '{}': {}.", o.key, ToString(gov.AssignOfficer(*f, o.id)));
        }
        for (const auto p : ComputeStats(*f).majorityProvinces) {
            logger::info("Reino (debug): governador de {}: {}.", Config::Get().NameOf(p), ToString(gov.AssignGovernor(*f, p)));
        }
        I18n::NotifyTr("realm.debug.conquered", n, Factions::Get().NameOf(*f));
        ShowSummary();
    }
}
