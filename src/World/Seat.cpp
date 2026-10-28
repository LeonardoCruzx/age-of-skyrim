#include "World/Seat.h"

#include "Core/Factions.h"
#include "Core/I18n.h"
#include "Core/State.h"
#include "Core/Timer.h"
#include "Sim/Economy.h"
#include "UI/Prompt.h"
#include "World/Discovery.h"
#include "World/Placement.h"

#include <map>

namespace AoS
{
    namespace
    {
        const fs::path  kDir = "Data/SKSE/Plugins/AgeOfSkyrim/Seats";
        constexpr float kPi  = 3.14159265f;

        std::string Lower(std::string_view a_text)
        {
            std::string out{ a_text };
            std::ranges::transform(out, out.begin(), [](char c) { return static_cast<char>(std::tolower(c)); });
            return out;
        }

        bool FromSkyrimEsm(const RE::TESForm* a_form)
        {
            const auto file = a_form ? a_form->GetFile(0) : nullptr;
            return file && _stricmp(file->fileName, "Skyrim.esm") == 0;
        }

        std::string ModelOf(const RE::TESModel& a_model)
        {
            const auto m = a_model.GetModel();
            return Lower(m ? m : "");
        }

        // Baú automático: CONT do Skyrim.esm SEM respawn (o conteúdo nunca é resetado), sem itens no objeto base,
        // com nome e modelo de baú. Preferência para o baú "noble"; ordem do arquivo = determinística.
        RE::TESObjectCONT* AutoChest()
        {
            const auto dh = RE::TESDataHandler::GetSingleton();
            if (!dh) {
                return nullptr;
            }
            for (const bool preferNoble : { true, false }) {
                for (const auto cont : dh->GetFormArray<RE::TESObjectCONT>()) {
                    if (!cont || !FromSkyrimEsm(cont) || cont->data.flags.any(RE::CONT_DATA::Flag::kRespawn) ||
                        cont->numContainerObjects != 0) {
                        continue;
                    }
                    const auto name = cont->GetFullName();
                    if (!name || !*name) {
                        continue;
                    }
                    const auto model = ModelOf(*cont);
                    if (model.find("chest") != std::string::npos &&
                        (!preferNoble || model.find("noble") != std::string::npos)) {
                        return cont;
                    }
                }
            }
            return nullptr;
        }

        // Mesa de guerra automática: ACTI do Skyrim.esm com nome (ativável pelo jogador) e modelo de mesa/mapa.
        // Sem ACTI adequado: STAT de mesa de mapa (só visual, sem ativação) — logado.
        RE::TESBoundObject* AutoWarTable()
        {
            const auto dh = RE::TESDataHandler::GetSingleton();
            if (!dh) {
                return nullptr;
            }
            for (const auto* needle : { "wartable", "maptable", "table" }) {
                for (const auto acti : dh->GetFormArray<RE::TESObjectACTI>()) {
                    if (!acti || !FromSkyrimEsm(acti)) {
                        continue;
                    }
                    const auto name = acti->GetFullName();
                    if (name && *name && ModelOf(*acti).find(needle) != std::string::npos) {
                        return acti;
                    }
                }
            }
            for (const auto* needle : { "wartable", "maptable" }) {
                for (const auto stat : dh->GetFormArray<RE::TESObjectSTAT>()) {
                    if (stat && FromSkyrimEsm(stat) && ModelOf(*stat).find(needle) != std::string::npos) {
                        logger::warn("Sede: nenhum ativador de mesa encontrado; usando um estático (sem ativação).");
                        return stat;
                    }
                }
            }
            return nullptr;
        }

        void LogBase(std::string_view a_what, RE::TESBoundObject* a_base)
        {
            if (!a_base) {
                logger::warn("Sede: nenhum objeto base para {}.", a_what);
                return;
            }
            const auto model = a_base->As<RE::TESModel>();
            logger::info("Sede: {} = {} ('{}', tipo {}, modelo '{}').", a_what, Discovery::KeyOf(a_base), a_base->GetName(),
                         RE::FormTypeToString(a_base->GetFormType()), model && model->GetModel() ? model->GetModel() : "?");
        }

        std::optional<RE::NiPoint3> ReadOffset(const nlohmann::json& a_obj, const char* a_name)
        {
            const auto v = a_obj.value(a_name, std::vector<float>{});
            if (v.size() == 3) {
                return RE::NiPoint3{ v[0], v[1], v[2] };
            }
            return std::nullopt;
        }

        // Cria o objeto relativo ao jogador (x = direita, y = frente, z = cima), virado para ele.
        RE::TESObjectREFR* PlaceRelative(RE::TESBoundObject* a_base, const RE::NiPoint3& a_offset)
        {
            const auto player = RE::PlayerCharacter::GetSingleton();
            if (!a_base || !player) {
                return nullptr;
            }
            const float  h   = player->GetAngleZ();
            RE::NiPoint3 pos = player->GetPosition();
            pos.x += a_offset.x * std::cos(h) + a_offset.y * std::sin(h);
            pos.y += -a_offset.x * std::sin(h) + a_offset.y * std::cos(h);
            pos.z += a_offset.z;
            float facing = h + kPi;
            if (facing > 2.0f * kPi) {
                facing -= 2.0f * kPi;
            }
            return Placement::PlaceAt(a_base, pos, { 0.0f, 0.0f, facing }, /*keyframed*/ false);
        }

        // Move tudo de um container para outro, preservando ExtraDataList (encantamentos, melhorias, nomes).
        std::int64_t TransferAll(RE::TESObjectREFR* a_from, RE::TESObjectREFR* a_to)
        {
            ScopedTimer  timer{ "Seat::TransferAll" };
            std::int64_t moved = 0;
            auto         inv   = a_from->GetInventory();
            for (auto& [object, data] : inv) {
                auto& [count, entry] = data;
                if (!object || count <= 0) {
                    continue;
                }
                std::vector<RE::ExtraDataList*> lists;  // cópia: RemoveItem mexe na lista original
                if (entry && entry->extraLists) {
                    for (const auto x : *entry->extraLists) {
                        if (x) {
                            lists.push_back(x);
                        }
                    }
                }
                std::int32_t remaining = count;
                for (const auto x : lists) {
                    const auto n = std::min(x->GetCount(), remaining);
                    if (n <= 0) {
                        continue;
                    }
                    a_from->RemoveItem(object, n, RE::ITEM_REMOVE_REASON::kStoreInContainer, x, a_to);
                    remaining -= n;
                    moved += n;
                }
                if (remaining > 0) {
                    a_from->RemoveItem(object, remaining, RE::ITEM_REMOVE_REASON::kStoreInContainer, nullptr, a_to);
                    moved += remaining;
                }
            }
            return moved;
        }

        void Destroy(RE::TESObjectREFR* a_ref)
        {
            if (a_ref && !a_ref->IsDeleted()) {
                a_ref->Disable();
                a_ref->SetDelete(true);
            }
        }

        RE::TESObjectREFR* LookupRef(RE::FormID a_id)
        {
            const auto ref = a_id ? RE::TESForm::LookupByID<RE::TESObjectREFR>(a_id) : nullptr;
            return ref && !ref->IsDeleted() ? ref : nullptr;
        }

        // M11: ativar a mesa de guerra da sede. Filtro por FormID atômico: custo de uma comparação por ativação.
        class ActivateSink final : public RE::BSTEventSink<RE::TESActivateEvent>
        {
        public:
            static ActivateSink* Get()
            {
                static ActivateSink instance;
                return std::addressof(instance);
            }

            RE::BSEventNotifyControl ProcessEvent(const RE::TESActivateEvent* a_event,
                                                  RE::BSTEventSource<RE::TESActivateEvent>*) override
            {
                const auto table = Seat::Get().WarTableId();
                if (table != 0 && a_event && a_event->objectActivated &&
                    a_event->objectActivated->GetFormID() == table && a_event->actionRef &&
                    a_event->actionRef->IsPlayerRef()) {
                    SKSE::GetTaskInterface()->AddTask([] { Seat::Get().OnWarTableActivated(); });
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };
    }

    Seat& Seat::Get() noexcept
    {
        static Seat instance;
        return instance;
    }

    // ---- Candidatas -------------------------------------------------------------------------------

    void Seat::Load()
    {
        ScopedTimer timer{ "Seat::Load" };
        {
            std::scoped_lock l{ lock_ };
            candidates_.clear();
        }
        std::error_code       ec;
        std::vector<fs::path> files;
        for (const auto& entry : fs::directory_iterator(kDir, ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                files.push_back(entry.path());
            }
        }
        std::sort(files.begin(), files.end());
        for (const auto& f : files) {
            LoadFile(f);
        }
        {
            std::scoped_lock l{ lock_ };
            logger::info("Sede: {} candidata(s) em {} arquivo(s).", candidates_.size(), files.size());
        }
        for (const auto id : Candidates()) {
            if (const auto t = Config::Get().Find(id)) {
                logger::info("Sede: candidata '{}' ({}, {}, província '{}').", t->name, t->key, ToString(t->type),
                             t->provinceName);
            }
        }

        ResolveBases();

        static bool registered = false;
        if (!registered) {
            registered = true;
            Conquest::Get().AddListener([](const Territory& a_t, Id a_prev, Conquest::Source a_src) {
                Seat::Get().OnConquered(a_t, a_prev, a_src);
            });
            if (const auto holder = RE::ScriptEventSourceHolder::GetSingleton()) {
                holder->AddEventSink<RE::TESActivateEvent>(ActivateSink::Get());
            }
        }
    }

    // Formato (Seats/*.json):
    //   { "auto": { "types": ["fort", "tower"], "perProvince": 2 },
    //     "candidates": [ { "territory": "id" } | { "location": "Plugin.esp|0xID" },
    //                     ... opcional "chestOffset": [direita, frente, cima], "warTableOffset": [...] ],
    //     "exclude": [ "Plugin.esp|0xID" | "id" ] }
    void Seat::LoadFile(const fs::path& a_file)
    {
        std::ifstream in(a_file);
        if (!in) {
            return;
        }
        nlohmann::json json;
        try {
            json = nlohmann::json::parse(in, nullptr, true, /*ignore_comments*/ true);
        } catch (const std::exception& e) {
            logger::error("JSON inválido em {}: {}", a_file.string(), e.what());
            return;
        }
        const auto& cfg  = Config::Get();
        const auto  file = a_file.filename().string();

        // "territory": id do JSON, ou "location": BGSLocation (descoberto ou do JSON).
        const auto resolve = [&](const nlohmann::json& a_entry) -> const Territory* {
            if (a_entry.is_string()) {
                const auto text = a_entry.get<std::string>();
                if (text.find('|') == std::string::npos) {
                    return cfg.Find(MakeId(text));
                }
                const auto ref = FormRef::Parse(text);
                return ref ? cfg.FindByLocation(ref->Resolve<RE::BGSLocation>()) : nullptr;
            }
            if (!a_entry.is_object()) {
                return nullptr;
            }
            if (const auto id = a_entry.value("territory", std::string{}); !id.empty()) {
                return cfg.Find(MakeId(id));
            }
            const auto ref = FormRef::Parse(a_entry.value("location", std::string{}));
            return ref ? cfg.FindByLocation(ref->Resolve<RE::BGSLocation>()) : nullptr;
        };

        std::unordered_map<Id, Candidate> found;

        // 1. Regra automática: por província, os N fortes/torres mais próximos da cidade da província.
        if (const auto rule = json.find("auto"); rule != json.end() && rule->is_object()) {
            std::vector<TerritoryType> types;
            for (const auto& v : rule->value("types", std::vector<std::string>{ "fort", "tower" })) {
                if (const auto type = ParseTerritoryType(v)) {
                    types.push_back(*type);
                } else {
                    logger::warn("{}: tipo '{}' inválido na regra automática.", file, v);
                }
            }
            const auto perProvince = rule->value("perProvince", std::size_t{ 2 });

            std::map<Id, std::vector<const Territory*>> byProvince;  // ordem determinística
            std::unordered_map<Id, const Territory*>    anchor;      // cidade da província
            for (const auto& t : cfg.Territories()) {
                if (t.province == kNeutral || !t.hasPosition) {
                    continue;
                }
                if (t.type == TerritoryType::kCity && !anchor.contains(t.province)) {
                    anchor.emplace(t.province, &t);
                }
                if (std::find(types.begin(), types.end(), t.type) != types.end()) {
                    byProvince[t.province].push_back(&t);
                }
            }
            for (auto& [province, list] : byProvince) {
                const auto a = anchor.find(province);
                std::sort(list.begin(), list.end(), [&](const Territory* x, const Territory* y) {
                    if (a != anchor.end() && x->worldspace == a->second->worldspace &&
                        y->worldspace == a->second->worldspace) {
                        return x->position.GetSquaredDistance(a->second->position) <
                               y->position.GetSquaredDistance(a->second->position);
                    }
                    return x->key < y->key;
                });
                for (std::size_t i = 0; i < std::min(perProvince, list.size()); ++i) {
                    found.try_emplace(list[i]->id, Candidate{ list[i]->id });
                }
            }
        }

        // 2. Lista explícita (sobrescreve os offsets das automáticas).
        for (const auto& entry : json.value("candidates", nlohmann::json::array())) {
            const auto t = resolve(entry);
            if (!t) {
                logger::warn("{}: candidata '{}' não encontrada (plugin ausente ou território desconhecido).", file,
                             entry.dump());
                continue;
            }
            Candidate c{ t->id };
            if (entry.is_object()) {
                c.chestOffset    = ReadOffset(entry, "chestOffset");
                c.warTableOffset = ReadOffset(entry, "warTableOffset");
            }
            found.insert_or_assign(t->id, c);
        }

        // 3. Exclusões deste arquivo valem para tudo o que já foi lido (inclusive arquivos anteriores).
        std::scoped_lock l{ lock_ };
        for (auto& [id, c] : found) {
            candidates_.insert_or_assign(id, c);
        }
        for (const auto& entry : json.value("exclude", nlohmann::json::array())) {
            if (const auto t = resolve(entry)) {
                candidates_.erase(t->id);
            }
        }
    }

    void Seat::ResolveBases()
    {
        const auto& s = Config::Get().Seat();
        chestBase_    = nullptr;
        warTableBase_ = nullptr;
        if (s.chest) {
            const auto base = s.chest->Resolve<RE::TESBoundObject>();
            const auto cont = base ? base->As<RE::TESObjectCONT>() : nullptr;
            if (!cont) {
                logger::warn("Sede: seat.chest {}|0x{:X} não é um container carregado; usando escolha automática.",
                             s.chest->plugin, s.chest->localId);
            } else if (cont->data.flags.any(RE::CONT_DATA::Flag::kRespawn)) {
                logger::warn("Sede: seat.chest {}|0x{:X} tem respawn (o conteúdo seria resetado); usando escolha automática.",
                             s.chest->plugin, s.chest->localId);
            } else {
                chestBase_ = cont;
            }
        }
        if (!chestBase_) {
            chestBase_ = AutoChest();
        }
        if (s.warTable) {
            warTableBase_ = s.warTable->Resolve<RE::TESBoundObject>();
            if (!warTableBase_) {
                logger::warn("Sede: seat.warTable {}|0x{:X} não encontrado; usando escolha automática.",
                             s.warTable->plugin, s.warTable->localId);
            }
        }
        if (!warTableBase_) {
            warTableBase_ = AutoWarTable();
        }
        LogBase("baú", chestBase_);
        LogBase("mesa de guerra", warTableBase_);
    }

    // ---- Consulta ---------------------------------------------------------------------------------

    bool Seat::IsCandidate(Id a_territory) const
    {
        std::scoped_lock l{ lock_ };
        return candidates_.contains(a_territory);
    }

    std::vector<Id> Seat::Candidates() const
    {
        std::scoped_lock l{ lock_ };
        std::vector<Id>  out;
        out.reserve(candidates_.size());
        for (const auto& [id, c] : candidates_) {
            out.push_back(id);
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    bool Seat::HasSeat() const
    {
        std::scoped_lock l{ lock_ };
        return territory_ != kNeutral;
    }

    Id Seat::SeatTerritory() const
    {
        std::scoped_lock l{ lock_ };
        return territory_;
    }

    RE::FormID Seat::ChestId() const
    {
        std::scoped_lock l{ lock_ };
        return chest_;
    }

    RE::TESObjectREFR* Seat::Chest() const { return LookupRef(ChestId()); }

    RE::TESObjectREFR* Seat::WarTable() const
    {
        RE::FormID id{};
        {
            std::scoped_lock l{ lock_ };
            id = warTable_;
        }
        return LookupRef(id);
    }

    std::optional<Stock::Totals> Seat::StockTotals() const
    {
        const auto chest = ChestId();
        return chest ? Stock::Get().LastTotals(chest) : std::nullopt;
    }

    // ---- Ações ------------------------------------------------------------------------------------

    bool Seat::Found(Id a_territory)
    {
        ScopedTimer timer{ "Seat::Found" };
        const auto* t = Config::Get().Find(a_territory);
        if (!t) {
            logger::warn("Sede: território 0x{:08X} desconhecido.", a_territory);
            return false;
        }
        const auto faction = Factions::Get().PlayerFaction();
        if (!faction || State::Get().Owner(a_territory) != faction->id) {
            I18n::NotifyTr("seat.notOwned", t->name);
            return false;
        }
        if (SeatTerritory() == a_territory && Chest()) {
            I18n::NotifyTr("seat.already", t->name);
            return false;
        }
        const auto player = RE::PlayerCharacter::GetSingleton();
        if (!chestBase_ || !player || !player->GetParentCell() || !player->Is3DLoaded()) {
            logger::error("Sede: sem objeto base do baú ou jogador sem célula carregada.");
            I18n::NotifyTr("seat.failed");
            return false;
        }

        // Ainda sem coordenadas curadas: posição relativa ao jogador no momento da fundação (ADR 0023).
        const auto& s = Config::Get().Seat();
        Candidate   spec{ a_territory };
        {
            std::scoped_lock l{ lock_ };
            if (const auto it = candidates_.find(a_territory); it != candidates_.end()) {
                spec = it->second;
            }
        }
        const auto newChest = PlaceRelative(chestBase_, spec.chestOffset.value_or(s.chestOffset));
        if (!newChest) {
            I18n::NotifyTr("seat.failed");
            return false;
        }
        const auto newTable = warTableBase_ ? PlaceRelative(warTableBase_, spec.warTableOffset.value_or(s.warTableOffset))
                                            : nullptr;
        if (warTableBase_ && !newTable) {
            logger::warn("Sede: mesa de guerra não pôde ser criada; a sede segue sem ela.");
        }

        // Mudança de sede: o conteúdo vai para o baú novo e os objetos antigos são apagados.
        const auto   oldTerritory = SeatTerritory();
        const auto   oldChest     = Chest();
        const auto   oldTable     = WarTable();
        std::int64_t moved        = 0;
        if (oldChest) {
            moved = TransferAll(oldChest, newChest);
            Stock::Get().Unobserve(oldChest->GetFormID());
        }
        Destroy(oldChest);
        Destroy(oldTable);

        {
            std::scoped_lock l{ lock_ };
            territory_ = a_territory;
            chest_     = newChest->GetFormID();
            warTable_  = newTable ? newTable->GetFormID() : 0;
        }
        warTableId_.store(newTable ? newTable->GetFormID() : 0, std::memory_order_relaxed);
        Stock::Get().Observe(newChest->GetFormID());
        Stock::Get().Recount(newChest->GetFormID());
        Economy::Get().SetDepot(newChest);  // M12: produção pendente entra no baú novo

        logger::info("Sede: {} em '{}' ({}): baú 0x{:08X}, mesa 0x{:08X}{}.", oldTerritory == kNeutral ? "fundada" : "mudada",
                     t->name, t->key, newChest->GetFormID(), newTable ? newTable->GetFormID() : 0,
                     oldTerritory == kNeutral ? "" : std::format(", {} item(ns) transferido(s) do baú antigo", moved));
        if (oldTerritory == kNeutral) {
            I18n::NotifyTr("seat.founded", t->name);
        } else {
            I18n::NotifyTr("seat.moved", t->name, moved);
        }
        // TODO(M9): disparar OnSeatFounded para a API.
        return true;
    }

    void Seat::Offer(const Territory& a_territory)
    {
        const auto current = SeatTerritory();
        if (current == a_territory.id) {
            return;
        }
        const bool  move    = current != kNeutral;
        const auto  id      = a_territory.id;
        const auto* old     = move ? Config::Get().Find(current) : nullptr;
        const auto  oldName = old ? old->name : std::string{ "?" };

        if (!Config::Get().Seat().askBeforeFounding) {
            if (!move) {
                Found(id);  // sem pergunta: só funda a primeira; mudar exige confirmação (M10/M11)
            }
            return;
        }
        const auto body = move ? I18n::Tr("seat.askMove", oldName, a_territory.name) :
                                 I18n::Tr("seat.askFound", a_territory.name);
        const bool asked = Prompt::Ask(body, { I18n::Tr("seat.yes"), I18n::Tr("seat.no") }, [id](std::size_t a_choice) {
            if (a_choice == 0) {
                Seat::Get().Found(id);
            } else {
                logger::info("Sede: jogador recusou (botão {}).", a_choice == Prompt::kNoChoice ? -1 : static_cast<int>(a_choice));
            }
        });
        if (!asked && !move) {
            Found(id);  // sem MessageBox disponível: funda direto (ADR 0023)
        }
    }

    void Seat::OnConquered(const Territory& a_territory, Id, Conquest::Source)
    {
        if (IsCandidate(a_territory.id)) {
            Offer(a_territory);
        }
    }

    void Seat::OnWarTableActivated()
    {
        const auto* t = Config::Get().Find(SeatTerritory());
        logger::info("Sede: mesa de guerra ativada ('{}'). TODO(M11): abrir o mapa tático.", t ? t->key : "?");
        I18n::NotifyTr("seat.warTable", t ? t->name : "?");
    }

    void Seat::ReportStock()
    {
        if (!HasSeat()) {
            logger::info("Sede: nenhuma sede fundada (estoque da sede não exibido).");
            return;
        }
        const auto chest = Chest();
        if (!chest) {
            I18n::NotifyTr("seat.chestMissing");
            return;
        }
        Stock::Get().Recount(chest->GetFormID());
        const auto  totals = Stock::Get().LastTotals(chest->GetFormID());
        const auto* t      = Config::Get().Find(SeatTerritory());
        const auto& cats   = Stock::Get().Categories();
        I18n::NotifyTr("seat.stockTitle", t ? t->name : "?");
        if (!totals) {
            return;
        }
        for (std::size_t i = 0; i < cats.size() && i < totals->perCategory.size(); ++i) {
            logger::info("Sede/estoque: {} ({}) = {}", cats[i].key, cats[i].name, totals->perCategory[i]);
            if (totals->perCategory[i] > 0) {
                I18n::NotifyTr("stock.line", cats[i].name, totals->perCategory[i]);
            }
        }
        I18n::NotifyTr("stock.unclassified", totals->unclassified);
    }

    // ---- Cosave -----------------------------------------------------------------------------------

    void Seat::OnPostLoad()
    {
        const auto chest = Chest();
        if (!HasSeat()) {
            return;
        }
        const auto* t = Config::Get().Find(SeatTerritory());
        logger::info("Sede: '{}' com baú 0x{:08X} e mesa 0x{:08X}.", t ? t->key : "?", ChestId(), WarTableId());
        if (!t) {
            logger::warn("Sede: o território da sede não existe mais na config (JSON/plugin removido); objetos mantidos.");
        }
        if (chest) {
            Stock::Get().Observe(chest->GetFormID());
            Stock::Get().Recount(chest->GetFormID());
            Economy::Get().SetDepot(chest);  // M12
        } else {
            logger::warn("Sede: baú da sede não encontrado depois de carregar.");
        }
    }

    void Seat::Clear()
    {
        std::scoped_lock l{ lock_ };
        if (chest_) {
            Stock::Get().Unobserve(chest_);
        }
        territory_ = kNeutral;
        chest_     = 0;
        warTable_  = 0;
        warTableId_.store(0, std::memory_order_relaxed);
    }

    // Formato do registro 'SEAT' v1:  u32 territoryId, u32 chestFormId, u32 warTableFormId (0 = sem mesa).
    void Seat::Save(SKSE::SerializationInterface* a_intfc) const
    {
        std::scoped_lock l{ lock_ };
        if (territory_ == kNeutral) {
            return;
        }
        if (!a_intfc->OpenRecord(kRecord, kRecordVersion)) {
            logger::error("Cosave: falha ao abrir o registro SEAT.");
            return;
        }
        a_intfc->WriteRecordData(territory_);
        a_intfc->WriteRecordData(chest_);
        a_intfc->WriteRecordData(warTable_);
    }

    void Seat::LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version)
    {
        if (a_version != kRecordVersion) {
            logger::warn("Cosave: versão {} do registro SEAT não suportada; sede descartada.", a_version);
            return;
        }
        Id         territory{};
        RE::FormID chest{}, table{};
        a_intfc->ReadRecordData(territory);
        a_intfc->ReadRecordData(chest);
        a_intfc->ReadRecordData(table);

        RE::FormID newChest{}, newTable{};
        if (!a_intfc->ResolveFormID(chest, newChest)) {
            logger::warn("Cosave: baú da sede 0x{:08X} não existe mais.", chest);
            newChest = 0;
        }
        if (table != 0 && !a_intfc->ResolveFormID(table, newTable)) {
            logger::warn("Cosave: mesa de guerra 0x{:08X} não existe mais.", table);
            newTable = 0;
        }
        {
            std::scoped_lock l{ lock_ };
            territory_ = territory;
            chest_     = newChest;
            warTable_  = newTable;
        }
        warTableId_.store(newTable, std::memory_order_relaxed);
        logger::info("Cosave: sede 0x{:08X} carregada (baú 0x{:08X}, mesa 0x{:08X}).", territory, newChest, newTable);
    }
}
