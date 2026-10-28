#include "World/Conquest.h"

#include "Banner/BannerController.h"
#include "Core/Factions.h"
#include "Core/I18n.h"
#include "Core/Timer.h"
#include "World/Discovery.h"

namespace AoS
{
    namespace
    {
        constexpr int kMaxDepth = 16;  // cadeia de locais (sub-local -> local -> Hold -> Skyrim)

        // O evento não diz QUAL local foi limpo: só agenda a checagem da cadeia do jogador para o próximo
        // frame (o flag "cleared" do BGSLocation já está gravado quando a task roda).
        class ClearedSink final : public RE::BSTEventSink<RE::LocationCleared::Event>
        {
        public:
            static ClearedSink* Get()
            {
                static ClearedSink instance;
                return std::addressof(instance);
            }

            RE::BSEventNotifyControl ProcessEvent(const RE::LocationCleared::Event*,
                                                  RE::BSTEventSource<RE::LocationCleared::Event>*) override
            {
                SKSE::GetTaskInterface()->AddTask([] { Conquest::Get().OnLocationCleared(); });
                return RE::BSEventNotifyControl::kContinue;
            }
        };

        // Guarda o local de onde o jogador saiu (requirePlayerInLocation = false). Custo: uma comparação por evento.
        class LocationChangeSink final : public RE::BSTEventSink<RE::TESActorLocationChangeEvent>
        {
        public:
            static LocationChangeSink* Get()
            {
                static LocationChangeSink instance;
                return std::addressof(instance);
            }

            RE::BSEventNotifyControl ProcessEvent(const RE::TESActorLocationChangeEvent* a_event,
                                                  RE::BSTEventSource<RE::TESActorLocationChangeEvent>*) override
            {
                if (a_event && a_event->actor && a_event->actor->IsPlayerRef()) {
                    Conquest::Get().OnPlayerLocationChanged(a_event->oldLoc, a_event->newLoc);
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };

        const Territory* FindInChain(RE::BGSLocation* a_location, bool a_onlyCleared)
        {
            const auto& cfg = Config::Get();
            auto        loc = a_location;
            for (int depth = 0; loc && depth < kMaxDepth; ++depth, loc = loc->parentLoc) {
                if (const auto t = cfg.FindByLocation(loc); t && (!a_onlyCleared || loc->IsCleared())) {
                    return t;
                }
            }
            return nullptr;
        }

        std::string DescribeChain(RE::BGSLocation* a_location)
        {
            std::string out;
            auto        loc = a_location;
            for (int depth = 0; loc && depth < kMaxDepth; ++depth, loc = loc->parentLoc) {
                const auto name = loc->GetFullName();
                out += std::format("{}{} ({}{})", out.empty() ? "" : " -> ", name && *name ? name : "?",
                                   Discovery::KeyOf(loc), loc->IsCleared() ? ", limpo" : "");
            }
            return out.empty() ? "nenhum" : out;
        }
    }

    Conquest& Conquest::Get() noexcept
    {
        static Conquest instance;
        return instance;
    }

    void Conquest::Register()
    {
        static bool registered = false;
        if (registered) {
            return;
        }
        if (const auto source = RE::LocationCleared::GetEventSource()) {
            source->AddEventSink(ClearedSink::Get());
            registered = true;
        } else {
            logger::error("Conquista: fonte do evento LocationCleared indisponível; conquista por local limpo desligada.");
        }
        if (const auto holder = RE::ScriptEventSourceHolder::GetSingleton()) {
            holder->AddEventSink<RE::TESActorLocationChangeEvent>(LocationChangeSink::Get());
        }
        const auto& c = Config::Get().Conquest();
        logger::info("Conquista: {} (exigir jogador no local: {}, só de neutro: {}, {} exclusão(ões), {} plugin(s) excluído(s)).",
                     c.enabled ? "ligada" : "desligada", c.requirePlayerInLocation, c.onlyFromNeutral, c.exclude.size(),
                     c.excludePlugins.size());
    }

    void Conquest::AddListener(Listener a_listener) { listeners_.push_back(std::move(a_listener)); }

    std::optional<Id> Conquest::EnsurePlayerFaction()
    {
        auto& factions = Factions::Get();
        if (const auto f = factions.PlayerFaction()) {
            return f->id;
        }
        // Estilo: o de settings, senão o primeiro que nenhuma facção pré-definida usa, senão o último declarado.
        const auto& cfg    = Config::Get();
        const auto& styles = cfg.Styles();
        if (styles.empty()) {
            logger::error("Conquista: nenhum estilo de bandeira declarado; não dá para criar a facção do jogador.");
            return std::nullopt;
        }
        Id style = kNeutral;
        if (const auto& wanted = cfg.Conquest().defaultStyle; !wanted.empty()) {
            if (std::find(styles.begin(), styles.end(), MakeId(wanted)) != styles.end()) {
                style = MakeId(wanted);
            } else {
                logger::warn("Conquista: conquest.defaultStyle '{}' não declarado; escolhendo automaticamente.", wanted);
            }
        }
        if (style == kNeutral) {
            const auto all = factions.All();
            for (const auto s : styles) {
                if (std::none_of(all.begin(), all.end(), [&](const Faction& f) { return f.style == s; })) {
                    style = s;
                    break;
                }
            }
        }
        if (style == kNeutral) {
            style = styles.back();
        }
        const auto id = factions.CreatePlayerFaction(I18n::Tr("conquest.defaultFactionName"), style,
                                                     cfg.Conquest().defaultColor);
        if (id) {
            I18n::NotifyTr("conquest.factionCreated", factions.NameOf(*id));
        }
        return id;
    }

    const Territory* Conquest::TerritoryAtPlayer(bool a_onlyCleared) const
    {
        const auto player = RE::PlayerCharacter::GetSingleton();
        return player ? FindInChain(player->GetCurrentLocation(), a_onlyCleared) : nullptr;
    }

    bool Conquest::IsExcluded(const Territory& a_territory) const
    {
        const auto& c = Config::Get().Conquest();
        if (c.exclude.contains(a_territory.id)) {
            return true;
        }
        if (!a_territory.location) {
            return false;
        }
        if (c.exclude.contains(MakeId(Discovery::KeyOf(a_territory.location)))) {
            return true;
        }
        if (c.skipCivilWarForts && Discovery::IsMilitaryFort(a_territory.location)) {
            return true;  // M16/ADR 0042: forte da Guerra Civil (patch civil-war-mods ou settings)
        }
        if (!c.excludePlugins.empty()) {
            if (const auto file = a_territory.location->GetFile(0)) {
                std::string name{ file->GetFilename() };
                std::ranges::transform(name, name.begin(), [](char ch) { return static_cast<char>(std::tolower(ch)); });
                return std::find(c.excludePlugins.begin(), c.excludePlugins.end(), name) != c.excludePlugins.end();
            }
        }
        return false;
    }

    bool Conquest::ConquerForPlayer(const Territory& a_territory, Source a_source)
    {
        ScopedTimer timer{ "Conquest::ConquerForPlayer" };
        const bool  debug = a_source == Source::kDebug;
        if (IsExcluded(a_territory)) {
            logger::info("Conquista: '{}' está na lista de exclusão (settings.conquest); ignorado.", a_territory.key);
            if (debug) {
                I18n::NotifyTr("conquest.excluded", a_territory.name);
            }
            return false;
        }
        const auto faction = EnsurePlayerFaction();
        if (!faction) {
            return false;
        }
        auto&      factions = Factions::Get();
        const auto previous = State::Get().Owner(a_territory.id);
        if (previous == *faction) {
            logger::info("Conquista: '{}' já é da facção do jogador.", a_territory.key);
            if (debug) {
                I18n::NotifyTr("conquest.alreadyYours", a_territory.name);
            }
            return false;
        }
        if (Config::Get().Conquest().onlyFromNeutral && previous != kNeutral) {
            logger::info("Conquista: '{}' é de '{}' e onlyFromNeutral está ligado; ignorado.", a_territory.key,
                         factions.NameOf(previous));
            if (debug) {
                I18n::NotifyTr("conquest.ownedByOther", a_territory.name, factions.NameOf(previous));
            }
            return false;
        }
        if (!SetTerritoryOwner(a_territory.id, *faction)) {
            return false;
        }
        logger::info("Conquista: '{}' ({}) tomado de '{}' por '{}' (origem: {}).", a_territory.key, a_territory.name,
                     factions.NameOf(previous), factions.NameOf(*faction),
                     a_source == Source::kCleared ? "local limpo" : debug ? "debug" : "api");
        I18n::NotifyTr("conquest.conquered", a_territory.name, factions.NameOf(*faction));
        for (const auto& l : listeners_) {
            l(a_territory, previous, a_source);
        }
        return true;
    }

    void Conquest::SimulateHere()
    {
        const auto player = RE::PlayerCharacter::GetSingleton();
        const auto loc    = player ? player->GetCurrentLocation() : nullptr;
        logger::info("Conquista (debug): cadeia de locais do jogador: {}", DescribeChain(loc));
        auto t = TerritoryAtPlayer(/*onlyCleared*/ false);
        if (!t) {
            // Fora de um território: usa o de teste (debug.territory), para testar a sede em qualquer lugar.
            t = Config::Get().Find(Config::Get().Debug().territory);
            if (!t) {
                I18n::NotifyTr("conquest.noTerritoryHere");
                return;
            }
            logger::info("Conquista (debug): nenhum território aqui; usando debug.territory '{}'.", t->key);
        }
        const auto previous = State::Get().Owner(t->id);
        if (!ConquerForPlayer(*t, Source::kDebug)) {
            // Já era do jogador: reexecuta os ganchos (ex.: oferecer a sede de novo), para repetir o teste.
            const auto mine = Factions::Get().PlayerFaction();
            if (mine && previous == mine->id) {
                for (const auto& l : listeners_) {
                    l(*t, previous, Source::kDebug);
                }
            }
        }
    }

    void Conquest::OnLocationCleared()
    {
        const auto& c = Config::Get().Conquest();
        if (!c.enabled) {
            return;
        }
        const auto player = RE::PlayerCharacter::GetSingleton();
        const auto loc    = player ? player->GetCurrentLocation() : nullptr;
        auto       t      = FindInChain(loc, /*onlyCleared*/ true);
        if (!t && !c.requirePlayerInLocation && previous_) {
            t = FindInChain(previous_, /*onlyCleared*/ true);  // último inimigo morreu logo depois de o jogador sair
        }
        if (!t) {
            logger::info("Conquista: local limpo sem território (cadeia do jogador: {}).", DescribeChain(loc));
            return;
        }
        ConquerForPlayer(*t, Source::kCleared);
    }

    void Conquest::OnPlayerLocationChanged(RE::BGSLocation* a_old, RE::BGSLocation*) { previous_ = a_old; }
}
