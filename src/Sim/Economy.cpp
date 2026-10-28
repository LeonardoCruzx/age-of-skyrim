#include "Sim/Economy.h"

#include "Core/Factions.h"
#include "Core/I18n.h"
#include "Core/State.h"
#include "Core/Timer.h"
#include "Sim/Routes.h"
#include "Stock/Stock.h"

namespace AoS
{
    namespace
    {
        const fs::path kRoot = "Data/SKSE/Plugins/AgeOfSkyrim";

        [[nodiscard]] double HoursNow()
        {
            const auto cal = RE::Calendar::GetSingleton();
            return cal ? static_cast<double>(cal->GetHoursPassed()) : -1.0;  // -1 = indisponível
        }

        // Eventos baratos que já acontecem no jogo. Cada um custa uma comparação de float; o tick de verdade
        // só é agendado quando passou >= 1 hora de jogo desde o último (ADR 0026).
        class TimeSink final :
            public RE::BSTEventSink<RE::MenuOpenCloseEvent>,
            public RE::BSTEventSink<RE::TESCellFullyLoadedEvent>,
            public RE::BSTEventSink<RE::TESSleepStopEvent>,
            public RE::BSTEventSink<RE::TESWaitStopEvent>
        {
        public:
            static TimeSink* Get()
            {
                static TimeSink instance;
                return std::addressof(instance);
            }

            RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event,
                                                  RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
            {
                if (a_event && !a_event->opening) {
                    Economy::Get().OnTimeEvent();
                }
                return RE::BSEventNotifyControl::kContinue;
            }

            RE::BSEventNotifyControl ProcessEvent(const RE::TESCellFullyLoadedEvent*,
                                                  RE::BSTEventSource<RE::TESCellFullyLoadedEvent>*) override
            {
                Economy::Get().OnTimeEvent();
                return RE::BSEventNotifyControl::kContinue;
            }

            RE::BSEventNotifyControl ProcessEvent(const RE::TESSleepStopEvent*,
                                                  RE::BSTEventSource<RE::TESSleepStopEvent>*) override
            {
                Economy::Get().OnTimeEvent();
                return RE::BSEventNotifyControl::kContinue;
            }

            RE::BSEventNotifyControl ProcessEvent(const RE::TESWaitStopEvent*,
                                                  RE::BSTEventSource<RE::TESWaitStopEvent>*) override
            {
                Economy::Get().OnTimeEvent();
                return RE::BSEventNotifyControl::kContinue;
            }
        };

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

    Economy& Economy::Get() noexcept
    {
        static Economy instance;
        return instance;
    }

    // ---- Dados ------------------------------------------------------------------------------------

    void Economy::Load()
    {
        ScopedTimer timer{ "Economy::Load" };
        {
            std::scoped_lock l{ lock_ };
            resources_.clear();
            resourceIndex_.clear();
            for (auto& p : production_) {
                p.clear();
            }
            factionMultiplier_.clear();
            playerMultiplier_ = 1.0;

            // Economy.json primeiro, depois Economy/*.json em ordem alfabética (outros mods sobrescrevem/somam).
            auto routes = nlohmann::json::object();
            std::vector<fs::path> files{ kRoot / "Economy.json" };
            std::vector<fs::path> extra;
            std::error_code       ec;
            for (const auto& entry : fs::directory_iterator(kRoot / "Economy", ec)) {
                if (entry.is_regular_file() && entry.path().extension() == ".json") {
                    extra.push_back(entry.path());
                }
            }
            std::sort(extra.begin(), extra.end());
            files.insert(files.end(), extra.begin(), extra.end());
            for (const auto& f : files) {
                try {
                    LoadFile(f, routes);
                } catch (const std::exception& e) {  // tipo errado num campo: avisa e segue com o resto
                    logger::error("Economia: erro em {}: {}", f.filename().string(), e.what());
                }
            }
            try {
                Routes::Get().Configure(routes);
            } catch (const std::exception& e) {
                logger::error("Economia: erro em \"routes\": {}", e.what());
            }

            std::size_t entries = 0;
            for (const auto& p : production_) {
                entries += p.size();
            }
            logger::info("Economia: {} recurso(s), {} regra(s) de produção, catch-up máx. {} h.", resources_.size(),
                         entries, maxCatchUpHours_);
        }

        static bool registered = false;
        if (!registered) {
            if (const auto ui = RE::UI::GetSingleton()) {
                ui->AddEventSink<RE::MenuOpenCloseEvent>(TimeSink::Get());
            }
            if (const auto holder = RE::ScriptEventSourceHolder::GetSingleton()) {
                holder->AddEventSink<RE::TESCellFullyLoadedEvent>(TimeSink::Get());
                holder->AddEventSink<RE::TESSleepStopEvent>(TimeSink::Get());
                holder->AddEventSink<RE::TESWaitStopEvent>(TimeSink::Get());
            }
            registered = true;
        }
    }

    void Economy::LoadFile(const fs::path& a_file, nlohmann::json& a_routes)
    {
        const auto json = ReadJson(a_file);
        if (!json || !json->is_object()) {
            if (a_file.filename() == "Economy.json") {
                logger::warn("Economia: {} ausente ou inválido; nada será produzido.", a_file.string());
            }
            return;
        }
        const auto name = a_file.filename().string();
        maxCatchUpHours_ = json->value("maxCatchUpHours", maxCatchUpHours_);

        // Categorias conhecidas do estoque (só para avisar de erro de digitação).
        std::unordered_set<std::string> categories;
        for (const auto& c : Stock::Get().Categories()) {
            categories.insert(c.key);
        }

        for (const auto& jr : json->value("resources", nlohmann::json::array())) {
            if (!jr.is_object()) {
                continue;
            }
            const auto key  = jr.value("id", std::string{});
            const auto item = FormRef::Parse(jr.value("item", std::string{}));
            if (key.empty() || !item) {
                logger::warn("{}: recurso sem 'id' ou 'item' válido (\"Plugin.esp|0xID\"); ignorado.", name);
                continue;
            }
            const auto form = item->Resolve<RE::TESForm>();
            if (!form || !form->IsBoundObject()) {
                logger::warn("{}: recurso '{}': item {}|0x{:X} não encontrado (plugin ausente?) ou não é um item.",
                             name, key, item->plugin, item->localId);
                continue;
            }
            Resource r;
            r.id       = MakeId(key);
            r.key      = key;
            r.category = jr.value("category", std::string{});
            r.item     = *item;
            r.object   = static_cast<RE::TESBoundObject*>(form);
            if (!r.category.empty() && !categories.empty() && !categories.contains(r.category)) {
                logger::warn("{}: recurso '{}': categoria '{}' não existe em Resources.json.", name, key, r.category);
            }
            const auto display = form->GetName();
            logger::info("Economia: recurso '{}' = {} ({}|0x{:06X}, categoria '{}').", key,
                         display && *display ? display : "?", item->plugin, item->localId, r.category);
            if (const auto it = resourceIndex_.find(r.id); it != resourceIndex_.end()) {
                resources_[it->second] = std::move(r);  // arquivo posterior substitui
            } else {
                resourceIndex_.emplace(r.id, resources_.size());
                resources_.push_back(std::move(r));
            }
        }

        // "production": { "village": { "bread": 0.5 } } — quantidade por hora; arquivo posterior substitui a regra.
        if (const auto prod = json->find("production"); prod != json->end() && prod->is_object()) {
            for (const auto& [typeKey, list] : prod->items()) {
                const auto type = typeKey == "unknown" ? std::optional{ TerritoryType::kUnknown }
                                                       : ParseTerritoryType(typeKey);
                if (!type || !list.is_object()) {
                    logger::warn("{}: tipo de território '{}' inválido em production; ignorado.", name, typeKey);
                    continue;
                }
                auto& rules = production_[static_cast<std::size_t>(*type)];
                for (const auto& [resKey, amount] : list.items()) {
                    const auto idx = IndexOf(MakeId(resKey));
                    if (!idx || !amount.is_number()) {
                        logger::warn("{}: production.{}: recurso '{}' desconhecido; ignorado.", name, typeKey, resKey);
                        continue;
                    }
                    std::erase_if(rules, [&](const auto& r) { return r.first == *idx; });
                    if (const auto v = amount.get<double>(); v > 0.0) {
                        rules.emplace_back(*idx, v);
                    }
                }
            }
        }

        // "factionMultiplier": { "imperial": 1.2, "player": 1.0 } ("player" = todas as facções do jogador).
        if (const auto fm = json->find("factionMultiplier"); fm != json->end() && fm->is_object()) {
            for (const auto& [key, value] : fm->items()) {
                if (!value.is_number()) {
                    continue;
                }
                if (key == "player") {
                    playerMultiplier_ = value.get<double>();
                } else {
                    factionMultiplier_[MakeId(key)] = value.get<double>();
                }
            }
        }

        if (const auto r = json->find("routes"); r != json->end() && r->is_object()) {
            a_routes.merge_patch(*r);
        }
    }

    std::optional<std::size_t> Economy::IndexOf(Id a_resource) const
    {
        const auto it = resourceIndex_.find(a_resource);
        return it == resourceIndex_.end() ? std::nullopt : std::optional{ it->second };
    }

    const Economy::Resource* Economy::FindResource(Id a_resource) const
    {
        const auto idx = IndexOf(a_resource);
        return idx ? &resources_[*idx] : nullptr;
    }

    double Economy::Multiplier(Id a_faction, bool a_player) const
    {
        if (const auto it = factionMultiplier_.find(a_faction); it != factionMultiplier_.end()) {
            return it->second;
        }
        return a_player ? playerMultiplier_ : 1.0;
    }

    Economy::Ledger& Economy::LedgerOf(Id a_faction)
    {
        auto& l = ledgers_[a_faction];
        if (l.balance.size() != resources_.size()) {
            l.balance.resize(resources_.size(), 0.0);
            l.deficit.resize(resources_.size(), 0.0);
        }
        return l;
    }

    // ---- Depósito ------------------------------------------------------------------------------------

    void Economy::SetDepot(RE::TESObjectREFR* a_chest)
    {
        std::scoped_lock l{ lock_ };
        depot_ = a_chest ? a_chest->GetFormID() : 0;
        logger::info("Economia: depósito = 0x{:08X}.", depot_);
        Report r;
        DeliverPending(r);
    }

    RE::FormID Economy::Depot() const
    {
        std::scoped_lock l{ lock_ };
        return depot_;
    }

    // Entrega a parte inteira do saldo pendente das facções do jogador, agrupada por item (uma chamada por item).
    // Thread principal (Tick roda via AddTask; SetDepot é chamado pela sede na thread principal).
    void Economy::DeliverPending(Report& a_report)
    {
        const auto chest = depot_ ? RE::TESForm::LookupByID<RE::TESObjectREFR>(depot_) : nullptr;
        a_report.hasDepot = chest != nullptr;
        if (!chest) {
            return;
        }
        std::vector<std::int32_t> total(resources_.size(), 0);
        for (const auto& f : Factions::Get().All()) {
            if (!f.player) {
                continue;
            }
            const auto it = ledgers_.find(f.id);
            if (it == ledgers_.end()) {
                continue;
            }
            auto& bal = it->second.balance;
            for (std::size_t i = 0; i < bal.size() && i < resources_.size(); ++i) {
                const auto whole = std::floor(bal[i]);
                if (whole >= 1.0) {
                    const auto n = static_cast<std::int32_t>(std::min(whole, 1.0e6));
                    total[i] += n;
                    bal[i] -= n;
                }
            }
        }
        for (std::size_t i = 0; i < total.size(); ++i) {
            if (total[i] > 0 && resources_[i].object) {
                chest->AddObjectToContainer(resources_[i].object, nullptr, total[i], nullptr);
                a_report.delivered.emplace_back(resources_[i].id, total[i]);
                logger::info("Economia: {} x '{}' entregue(s) no depósito.", total[i], resources_[i].key);
            }
        }
    }

    // ---- Consumo -------------------------------------------------------------------------------------

    void Economy::RegisterUpkeep(std::string_view a_key, UpkeepProvider a_provider)
    {
        std::scoped_lock l{ lock_ };
        std::erase_if(upkeep_, [&](const auto& p) { return p.first == a_key; });
        upkeep_.emplace_back(std::string(a_key), std::move(a_provider));
    }

    void Economy::UnregisterUpkeep(std::string_view a_key)
    {
        std::scoped_lock l{ lock_ };
        std::erase_if(upkeep_, [&](const auto& p) { return p.first == a_key; });
    }

    // ---- M14/M15: ganchos do tick ----

    void Economy::AddTickListener(std::string_view a_key, TickListener a_listener)
    {
        std::scoped_lock l{ lock_ };
        std::erase_if(tickListeners_, [&](const auto& p) { return p.first == a_key; });
        tickListeners_.emplace_back(std::string(a_key), std::move(a_listener));
    }

    void Economy::SetTerritoryMultiplier(TerritoryMultiplier a_provider)
    {
        std::scoped_lock l{ lock_ };
        territoryMult_ = std::move(a_provider);
    }

    void Economy::NotifyTick(std::uint32_t a_hours)
    {
        if (a_hours == 0) {
            return;
        }
        std::vector<TickListener> listeners;
        {
            std::scoped_lock l{ lock_ };
            for (const auto& [key, fn] : tickListeners_) {
                listeners.push_back(fn);
            }
        }
        for (const auto& fn : listeners) {
            if (fn) {
                fn(a_hours);
            }
        }
    }
    // ---- fim M14/M15 ----

    bool Economy::InDeficit(Id a_faction) const
    {
        std::scoped_lock l{ lock_ };
        const auto       it = ledgers_.find(a_faction);
        return it != ledgers_.end() && it->second.deficitHours > 0;
    }

    double Economy::Deficit(Id a_faction, Id a_resource) const
    {
        std::scoped_lock l{ lock_ };
        const auto       it  = ledgers_.find(a_faction);
        const auto       idx = IndexOf(a_resource);
        return it != ledgers_.end() && idx && *idx < it->second.deficit.size() ? it->second.deficit[*idx] : 0.0;
    }

    std::uint32_t Economy::DeficitHours(Id a_faction) const
    {
        std::scoped_lock l{ lock_ };
        const auto       it = ledgers_.find(a_faction);
        return it == ledgers_.end() ? 0 : it->second.deficitHours;
    }

    double Economy::Balance(Id a_faction, Id a_resource) const
    {
        std::scoped_lock l{ lock_ };
        const auto       it  = ledgers_.find(a_faction);
        const auto       idx = IndexOf(a_resource);
        return it != ledgers_.end() && idx && *idx < it->second.balance.size() ? it->second.balance[*idx] : 0.0;
    }

    Economy::Report Economy::LastReport() const
    {
        std::scoped_lock l{ lock_ };
        return last_;
    }

    // ---- Tick ------------------------------------------------------------------------------------------

    void Economy::OnTimeEvent()
    {
        const auto now  = HoursNow();
        const auto last = lastHour_.load();
        if (now < 0.0 || (last != kUnset && now - last < 1.0 && now >= last)) {
            return;  // caminho comum: nada a fazer
        }
        if (!queued_.exchange(true)) {
            SKSE::GetTaskInterface()->AddTask([] {
                Economy::Get().queued_ = false;
                Economy::Get().Tick();
            });
        }
    }

    void Economy::Tick()
    {
        const auto now = HoursNow();
        if (now < 0.0) {
            return;
        }
        std::uint32_t processed = 0;
        {
            std::scoped_lock l{ lock_ };
            const auto       last = lastHour_.load();
            if (last == kUnset || now < last) {
                // Jogo novo / save sem registro ECON / relógio voltou: começa a contar daqui, sem produção retroativa.
                lastHour_ = now;
                return;
            }
            const auto elapsed = static_cast<std::uint32_t>(std::min(std::floor(now - last), 1.0e6));
            if (elapsed == 0) {
                return;
            }
            lastHour_ = last + elapsed;  // mantém a fração da hora corrente
            auto hours = elapsed;
            if (hours > maxCatchUpHours_) {
                logger::warn("Economia: {} h desde o último tick; processando só {} h (maxCatchUpHours).", hours,
                             maxCatchUpHours_);
                hours = maxCatchUpHours_;
            }
            if (hours > 0) {
                Process(hours);
                processed = hours;
            }
        }
        NotifyTick(processed);  // M14/M15: obras e guarnição, fora do lock
    }

    // Produção é linear nas horas: um lote de N horas custa o mesmo que 1 hora (O(territórios × regras)).
    void Economy::Process(std::uint32_t a_hours)
    {
        const auto start = std::chrono::steady_clock::now();
        Report     report;
        report.hours = a_hours;

        std::unordered_set<Id> players;
        for (const auto& f : Factions::Get().All()) {
            if (f.player) {
                players.insert(f.id);
            }
        }

        // 1. Produção por território.
        std::vector<double> playerProduced(resources_.size(), 0.0);
        const auto&         state = State::Get();
        for (const auto& t : Config::Get().Territories()) {
            const auto owner = state.Owner(t.id);
            if (owner == kNeutral) {
                continue;
            }
            const auto& rules = production_[static_cast<std::size_t>(t.type)];
            if (rules.empty()) {
                continue;
            }
            const bool player = players.contains(owner);
            auto       mult   = Multiplier(owner, player) * a_hours;
            if (territoryMult_) {
                mult *= std::max(0.0, territoryMult_(t.id));  // M15: melhoria de renda
            }
            auto&      ledger = LedgerOf(owner);
            for (const auto& [idx, perHour] : rules) {
                const auto amount = perHour * mult;
                ledger.balance[idx] += amount;
                if (player) {
                    playerProduced[idx] += amount;
                }
            }
            ++report.producing;
        }

        // 2. Rotas de comércio: ouro para a facção dona.
        Routes::Totals routeTotals;
        const auto     income = Routes::Get().Accrue(a_hours, routeTotals);
        report.routesActive  = routeTotals.active;
        report.routesBlocked = routeTotals.blocked;
        report.routeGold     = routeTotals.gold;
        if (const auto goldIdx = IndexOf(Routes::Get().ResourceKey())) {
            for (const auto& in : income) {
                LedgerOf(in.faction).balance[*goldIdx] += in.gold;
                if (players.contains(in.faction)) {
                    playerProduced[*goldIdx] += in.gold;
                }
            }
        } else if (!income.empty()) {
            logger::warn("Economia: recurso das rotas não existe em Economy.json; renda das rotas perdida.");
        }

        // 3. Consumo (manutenção registrada por outros módulos, ex.: guarnições na M14).
        std::vector<Upkeep> costs;
        for (const auto& [key, provider] : upkeep_) {
            if (provider) {
                provider(costs);
            }
        }
        for (auto& [id, ledger] : ledgers_) {
            std::fill(ledger.deficit.begin(), ledger.deficit.end(), 0.0);
        }
        std::unordered_map<Id, bool> paidAll;
        RE::TESObjectREFR*           chest = nullptr;
        std::optional<RE::TESObjectREFR::InventoryCountMap> inventory;
        for (const auto& c : costs) {
            const auto idx = IndexOf(c.resource);
            if (!idx || c.perHour <= 0.0 || c.faction == kNeutral) {
                continue;
            }
            auto& ledger = LedgerOf(c.faction);
            auto  need   = c.perHour * a_hours;
            const auto fromBalance = std::min(need, std::max(ledger.balance[*idx], 0.0));
            ledger.balance[*idx] -= fromBalance;
            need -= fromBalance;
            // Jogador: o que faltar sai dos itens reais do depósito.
            if (need > 0.0 && players.contains(c.faction) && depot_) {
                if (!inventory) {
                    chest = RE::TESForm::LookupByID<RE::TESObjectREFR>(depot_);
                    inventory = chest ? chest->GetInventoryCounts() : RE::TESObjectREFR::InventoryCountMap{};
                }
                const auto obj = resources_[*idx].object;
                if (const auto it = inventory->find(obj); chest && it != inventory->end() && it->second > 0) {
                    const auto take = std::min<std::int32_t>(it->second, static_cast<std::int32_t>(std::ceil(need)));
                    chest->RemoveItem(obj, take, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
                    it->second -= take;
                    ledger.balance[*idx] += take - std::min(need, static_cast<double>(take));  // troco fracionário
                    need = std::max(0.0, need - take);
                }
            }
            if (need > 1e-9) {
                ledger.deficit[*idx] += need;
                paidAll[c.faction] = false;
            } else {
                paidAll.try_emplace(c.faction, true);
            }
        }
        for (auto& [id, ledger] : ledgers_) {
            const auto it = paidAll.find(id);
            if (it != paidAll.end() && !it->second) {
                ledger.deficitHours += a_hours;
                if (players.contains(id)) {
                    report.deficit = true;
                }
            } else {
                ledger.deficitHours = 0;
            }
        }

        // 4. Entrega no depósito (facções do jogador).
        DeliverPending(report);

        for (std::size_t i = 0; i < playerProduced.size(); ++i) {
            if (playerProduced[i] > 0.0) {
                report.playerProduced.emplace_back(resources_[i].id, playerProduced[i]);
            }
        }
        report.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        logger::info("[tempo] Economy::Tick ({} h, {} território(s), {} rota(s) ativa(s), {} cortada(s)): {:.3f} ms",
                     a_hours, report.producing, report.routesActive, report.routesBlocked, report.ms);
        last_ = std::move(report);
    }

    void Economy::DebugForceHours(std::uint32_t a_hours)
    {
        Report r;
        {
            std::scoped_lock l{ lock_ };
            Process(std::max<std::uint32_t>(a_hours, 1));
            r = last_;
        }
        NotifyTick(r.hours);  // M14/M15: as horas forçadas também avançam obras e guarnição
        I18n::NotifyTr("economy.summary", r.hours, r.producing, std::format("{:.2f}", r.ms));
        for (const auto& [id, amount] : r.playerProduced) {
            const auto  res  = FindResource(id);
            const auto  name = res && res->object ? res->object->GetName() : nullptr;  // nome do jogo (localizado)
            std::string label = name && *name ? name : (res ? res->key : "?"s);
            I18n::NotifyTr("economy.produced", std::format("{:.1f}", amount), label);
        }
        I18n::NotifyTr("economy.routes", r.routesActive, r.routesBlocked, std::format("{:.1f}", r.routeGold));
        if (r.hasDepot) {
            std::int32_t items = 0;
            for (const auto& d : r.delivered) {
                items += d.second;
            }
            I18n::NotifyTr("economy.delivered", items);
        } else {
            I18n::NotifyTr("economy.noDepot");
        }
        if (r.deficit) {
            I18n::NotifyTr("economy.deficit");
        }
        // Saldos no log (todas as facções), para conferir a IA.
        std::scoped_lock l{ lock_ };
        for (const auto& [faction, ledger] : ledgers_) {
            for (std::size_t i = 0; i < ledger.balance.size() && i < resources_.size(); ++i) {
                if (ledger.balance[i] != 0.0) {
                    logger::info("Economia: saldo {} / {} = {:.2f}", Factions::Get().NameOf(faction), resources_[i].key,
                                 ledger.balance[i]);
                }
            }
        }
    }

    // ---- Cosave ----------------------------------------------------------------------------------------

    void Economy::Clear()
    {
        std::scoped_lock l{ lock_ };
        lastHour_ = kUnset;
        depot_    = 0;
        ledgers_.clear();
        last_ = {};
    }

    void Economy::Save(SKSE::SerializationInterface* a_intfc) const
    {
        std::scoped_lock l{ lock_ };
        if (!a_intfc->OpenRecord(kRecord, kRecordVersion)) {
            logger::error("Cosave: falha ao abrir o registro ECON.");
            return;
        }
        a_intfc->WriteRecordData(lastHour_.load());
        a_intfc->WriteRecordData(depot_);
        a_intfc->WriteRecordData(static_cast<std::uint32_t>(ledgers_.size()));
        for (const auto& [faction, ledger] : ledgers_) {
            a_intfc->WriteRecordData(faction);
            a_intfc->WriteRecordData(ledger.deficitHours);
            const auto n = std::min(ledger.balance.size(), resources_.size());
            a_intfc->WriteRecordData(static_cast<std::uint32_t>(n));
            for (std::size_t i = 0; i < n; ++i) {
                a_intfc->WriteRecordData(resources_[i].id);  // por Id: reordenar o JSON não corrompe
                a_intfc->WriteRecordData(ledger.balance[i]);
                a_intfc->WriteRecordData(ledger.deficit[i]);
            }
        }
    }

    void Economy::LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version)
    {
        if (a_version != kRecordVersion) {
            logger::warn("Cosave: versão {} do registro ECON não suportada; economia recomeça.", a_version);
            return;
        }
        std::scoped_lock l{ lock_ };
        double           last{};
        RE::FormID       depot{};
        std::uint32_t    factions{};
        a_intfc->ReadRecordData(last);
        a_intfc->ReadRecordData(depot);
        a_intfc->ReadRecordData(factions);
        lastHour_ = last;
        depot_    = 0;
        if (depot != 0 && !a_intfc->ResolveFormID(depot, depot_)) {
            logger::warn("Cosave: depósito da economia não existe mais; produção volta a acumular.");
        }
        std::size_t dropped = 0;
        for (std::uint32_t f = 0; f < factions; ++f) {
            Id            faction{};
            std::uint32_t hours{}, count{};
            a_intfc->ReadRecordData(faction);
            a_intfc->ReadRecordData(hours);
            a_intfc->ReadRecordData(count);
            auto& ledger        = LedgerOf(faction);
            ledger.deficitHours = hours;
            for (std::uint32_t i = 0; i < count; ++i) {
                Id     res{};
                double bal{}, def{};
                a_intfc->ReadRecordData(res);
                a_intfc->ReadRecordData(bal);
                a_intfc->ReadRecordData(def);
                if (const auto idx = IndexOf(res)) {
                    ledger.balance[*idx] = bal;
                    ledger.deficit[*idx] = def;
                } else {
                    ++dropped;  // recurso removido do JSON
                }
            }
        }
        logger::info("Cosave: economia carregada (último tick {:.1f} h, {} facção(ões), depósito 0x{:08X}, {} saldo(s) "
                     "de recurso removido descartado(s)).",
                     last, ledgers_.size(), depot_, dropped);
    }
}
