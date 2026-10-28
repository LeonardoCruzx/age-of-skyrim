#include "Realm/Jarl.h"

#include "Banner/BannerController.h"
#include "Core/Factions.h"
#include "Core/I18n.h"
#include "Core/State.h"
#include "Core/Timer.h"
#include "Realm/Government.h"
#include "Realm/Population.h"
#include "UI/Prompt.h"

namespace AoS::Realm
{
    namespace
    {
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

        bool CellAttached(const RE::TESObjectREFR* a_ref)
        {
            const auto cell = a_ref ? a_ref->GetParentCell() : nullptr;
            return cell && cell->IsAttached() && !a_ref->IsDeleted();
        }

        // Mesmo padrão de Garrison::ArmorMatches: dica no caminho do modelo da armadura (ou leveled item).
        bool ArmorMatches(RE::TESForm* a_item, const std::string& a_hint, int a_depth)
        {
            if (!a_item || a_hint.empty() || a_depth > 2) {
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

        // Escolha automática (sem FormIDs de memória, ADR 0056): NPC não-único do Skyrim.esm (nunca o jarl
        // vanilla, que é sempre único), de raça jogável, com nome, cujo outfit padrão bate com a_hint
        // ("noble" por padrão). FaceGen pronto (BSA vanilla) — mesma lição do Garrison::AutoSoldier.
        RE::TESNPC* AutoJarl(const std::string& a_hint)
        {
            const auto dh = RE::TESDataHandler::GetSingleton();
            if (!dh) {
                return nullptr;
            }
            RE::TESNPC* fallback = nullptr;
            for (const auto npc : dh->GetFormArray<RE::TESNPC>()) {
                if (!npc || !FromSkyrimEsm(npc) || npc->IsUnique() || !npc->defaultOutfit) {
                    continue;
                }
                const auto name = npc->GetName();
                if (!name || !*name || !npc->race || !npc->race->GetPlayable()) {
                    continue;
                }
                if (!fallback) {
                    fallback = npc;  // melhor que nada: primeiro NPC não-único válido, ordem do jogo
                }
                for (const auto item : npc->defaultOutfit->outfitItems) {
                    if (ArmorMatches(item, a_hint, 0)) {
                        return npc;
                    }
                }
            }
            if (fallback) {
                logger::warn("Governo local: nenhum NPC com outfit '{}'; usando '{}' sem a dica de roupa.", a_hint,
                             fallback->GetName());
            }
            return fallback;
        }

        // Marcador da capital: worldLocMarker do local, senão a primeira bandeira do território (mesmo padrão
        // de Garrison::FindAnchor / Upgrades::FindAnchor).
        RE::TESObjectREFR* FindAnchor(const Territory& a_territory)
        {
            if (a_territory.location) {
                if (const auto marker = a_territory.location->worldLocMarker.get(); marker && CellAttached(marker.get())) {
                    return marker.get();
                }
            }
            RE::TESObjectREFR* found = nullptr;
            for (const auto& set : a_territory.banners) {
                ForEachBanner(set, [&](RE::TESObjectREFR* a_ref) {
                    if (!found && CellAttached(a_ref)) {
                        found = a_ref;
                    }
                });
                if (found) {
                    break;
                }
            }
            return found;
        }

        // Cria o NPC perto do âncora (x = direita, y = frente, z = cima, no referencial do âncora).
        RE::TESObjectREFR* PlaceNear(RE::TESObjectREFR* a_anchor, RE::TESBoundObject* a_base, const RE::NiPoint3& a_offset)
        {
            if (!a_anchor || !a_base) {
                return nullptr;
            }
            const auto ref = a_anchor->PlaceObjectAtMe(a_base, /*forcePersist*/ true);
            if (!ref) {
                return nullptr;
            }
            const float  h   = a_anchor->GetAngleZ();
            RE::NiPoint3 pos = a_anchor->GetPosition();
            pos.x += a_offset.x * std::cos(h) + a_offset.y * std::sin(h);
            pos.y += -a_offset.x * std::sin(h) + a_offset.y * std::cos(h);
            pos.z += a_offset.z;
            ref->SetPosition(pos);
            return ref.get();
        }

        void EnsureOutfit(RE::Actor* a_actor, RE::BGSOutfit* a_override)
        {
            const auto npc    = a_actor ? a_actor->GetActorBase() : nullptr;
            const auto outfit = a_override ? a_override : (npc ? npc->defaultOutfit : nullptr);
            if (!outfit || !a_actor || a_actor->IsDead()) {
                return;
            }
            if (!a_actor->HasOutfitItems(outfit)) {
                a_actor->AddWornOutfit(outfit, true);
            }
        }

        // Hotkey de debug (settings.json -> debug.jarlHotkey). Trabalho na fila da thread principal.
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
                const auto key = Config::Get().Debug().jarlHotkey;
                for (auto e = *a_event; e; e = e->next) {
                    if (e->GetEventType() != RE::INPUT_EVENT_TYPE::kButton) {
                        continue;
                    }
                    const auto b = e->AsButtonEvent();
                    if (!b || !b->IsDown() || b->GetDevice() != RE::INPUT_DEVICE::kKeyboard ||
                        static_cast<int>(b->GetIDCode()) != key) {
                        continue;
                    }
                    if (const auto ui = RE::UI::GetSingleton(); ui && ui->GameIsPaused()) {
                        continue;
                    }
                    SKSE::GetTaskInterface()->AddTask([] { Jarl::Get().DebugOfferHere(); });
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };

        // Ativação do NPC do governador: filtro por FormID (poucas entradas, uma por província com presença).
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
                if (a_event && a_event->objectActivated && a_event->actionRef && a_event->actionRef->IsPlayerRef()) {
                    const auto id = a_event->objectActivated->GetFormID();
                    SKSE::GetTaskInterface()->AddTask([id] { Jarl::Get().OnActivated(id); });
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };
    }

    Jarl& Jarl::Get() noexcept
    {
        static Jarl instance;
        return instance;
    }

    void Jarl::ResolveBase()
    {
        const auto& s = Config::Get().Jarl();
        actorBase_     = nullptr;
        outfitBase_    = nullptr;
        if (s.actor) {
            actorBase_ = s.actor->Resolve<RE::TESNPC>();
            if (!actorBase_) {
                logger::warn("Governo local: jarl.actor {}|0x{:X} não encontrado; usando escolha automática.",
                             s.actor->plugin, s.actor->localId);
            }
        }
        if (!actorBase_) {
            actorBase_ = AutoJarl(Lower(s.autoHint));
        }
        if (s.outfit) {
            outfitBase_ = s.outfit->Resolve<RE::BGSOutfit>();
            if (!outfitBase_) {
                logger::warn("Governo local: jarl.outfit {}|0x{:X} não encontrado; usando o outfit padrão do NPC.",
                             s.outfit->plugin, s.outfit->localId);
            }
        }
        if (actorBase_) {
            logger::info("Governo local: NPC = 0x{:08X} ('{}').", actorBase_->GetFormID(), actorBase_->GetName());
        } else {
            logger::warn("Governo local: nenhum NPC base disponível; governadores ficam sem presença (TODO).");
        }
    }

    void Jarl::Load()
    {
        ScopedTimer timer{ "Jarl::Load" };
        ResolveBase();

        static bool registered = false;
        if (!registered) {
            registered = true;
            Conquest::Get().AddListener([](const Territory& a_t, Id a_prev, Conquest::Source a_src) {
                Jarl::Get().OnConquered(a_t, a_prev, a_src);
            });
            if (const auto holder = RE::ScriptEventSourceHolder::GetSingleton()) {
                holder->AddEventSink<RE::TESActivateEvent>(ActivateSink::Get());
            }
            if (Config::Get().Debug().enabled) {
                if (const auto input = RE::BSInputDeviceManager::GetSingleton()) {
                    input->AddEventSink(DebugSink::Get());
                    logger::info("Governo local: hotkey de debug 0x{:X}.", Config::Get().Debug().jarlHotkey);
                }
            }
        }
    }

    // ---- Consulta -----------------------------------------------------------------------------------

    Id Jarl::CapitalOf(Id a_province) const
    {
        if (a_province == kNeutral) {
            return kNeutral;
        }
        const Territory* bestCity = nullptr;
        const Territory* bestAny  = nullptr;
        float            bestCityPop = -1.0f, bestAnyPop = -1.0f;
        for (const auto& t : Config::Get().Territories()) {
            if (t.province != a_province) {
                continue;
            }
            const auto pop = Population::Get().Of(t.id);
            if (!bestAny || pop > bestAnyPop || (pop == bestAnyPop && t.key < bestAny->key)) {
                bestAny    = &t;
                bestAnyPop = pop;
            }
            if (t.type == TerritoryType::kCity &&
                (!bestCity || pop > bestCityPop || (pop == bestCityPop && t.key < bestCity->key))) {
                bestCity    = &t;
                bestCityPop = pop;
            }
        }
        // Sem cidade na província: cai para o território de maior população (heurística, ADR 0056).
        const auto chosen = bestCity ? bestCity : bestAny;
        return chosen ? chosen->id : kNeutral;
    }

    bool Jarl::HasNpc(Id a_province) const
    {
        std::scoped_lock l{ lock_ };
        const auto       it = byProvince_.find(a_province);
        return it != byProvince_.end() && it->second.npc != 0;
    }

    RE::FormID Jarl::NpcOf(Id a_province) const
    {
        std::scoped_lock l{ lock_ };
        const auto       it = byProvince_.find(a_province);
        return it == byProvince_.end() ? 0 : it->second.npc;
    }

    // ---- Ações --------------------------------------------------------------------------------------

    void Jarl::Offer(Id a_territory)
    {
        const auto t = Config::Get().Find(a_territory);
        if (!t || t->province == kNeutral) {
            logger::warn("Governo local: território 0x{:08X} sem província; oferta ignorada.", a_territory);
            return;
        }
        if (HasNpc(t->province)) {
            I18n::NotifyTr("jarl.already", std::string(Config::Get().NameOf(t->province)));
            return;
        }
        const auto faction = Factions::Get().PlayerFaction();
        if (!faction || State::Get().Owner(a_territory) != faction->id) {
            I18n::NotifyTr("jarl.notOwned", t->name);
            return;
        }
        if (!Config::Get().Jarl().askBeforeAssign) {
            Place(faction->id, a_territory);
            return;
        }
        const auto body  = I18n::Tr("jarl.askReplace", t->name, std::string(Config::Get().NameOf(t->province)));
        const auto id    = a_territory;
        const bool asked = Prompt::Ask(body, { I18n::Tr("jarl.yes"), I18n::Tr("jarl.no") }, [id](std::size_t a_choice) {
            if (a_choice == 0) {
                if (const auto f = Factions::Get().PlayerFaction()) {
                    Jarl::Get().Place(f->id, id);
                }
            } else {
                logger::info("Governo local: jogador recusou a oferta (botão {}).",
                             a_choice == Prompt::kNoChoice ? -1 : static_cast<int>(a_choice));
            }
        });
        if (!asked) {
            Place(faction->id, a_territory);  // sem MessageBox disponível: coloca direto (padrão do Seat)
        }
    }

    void Jarl::Place(Id a_faction, Id a_territory)
    {
        ScopedTimer timer{ "Jarl::Place" };
        const auto  t = Config::Get().Find(a_territory);
        if (!t || t->province == kNeutral) {
            return;
        }
        const auto province = t->province;

        // Reserva a vaga de governador (Government, M18) ANTES de criar o NPC: sem maioria/vaga, nada é criado.
        const auto result = Government::Get().AssignGovernor(a_faction, province);
        if (result != Government::Result::kOk) {
            switch (result) {
            case Government::Result::kNoMajority:
                I18n::NotifyTr("jarl.noMajority", std::string(Config::Get().NameOf(province)));
                break;
            case Government::Result::kNoSlot:
                I18n::NotifyTr("jarl.noSlot");
                break;
            default:
                I18n::NotifyTr("jarl.failed");
                break;
            }
            logger::info("Governo local: governador de '{}' não atribuído ({}).", Config::Get().NameOf(province),
                         ToString(result));
            return;
        }

        if (!actorBase_) {
            logger::warn("Governo local: sem NPC base; '{}' fica só com o bônus numérico (TODO).", t->key);
            I18n::NotifyTr("jarl.noActor", t->name);
            return;
        }
        const auto anchor = FindAnchor(*t);
        if (!anchor) {
            logger::warn("Governo local: marcador de '{}' não carregado; presença adiada.", t->key);
            I18n::NotifyTr("jarl.cellNotLoaded", t->name);
            return;  // a vaga de governador fica sem NPC (npc=0) até a próxima tentativa
        }
        const auto ref   = PlaceNear(anchor, actorBase_, Config::Get().Jarl().offset);
        const auto actor = ref ? ref->As<RE::Actor>() : nullptr;
        if (!actor) {
            logger::error("Governo local: PlaceObjectAtMe falhou para 0x{:08X}.", actorBase_->GetFormID());
            return;
        }
        EnsureOutfit(actor, outfitBase_);
        if (const auto playerFaction = RE::TESForm::LookupByID<RE::TESFaction>(0x00000DB1)) {  // PlayerFaction (ADR 0005: ref só)
            actor->AddToFaction(playerFaction, 0);
        }
        actor->EvaluatePackage();
        const auto npcId = actor->GetFormID();
        SKSE::GetTaskInterface()->AddTask([npcId] {  // "tropas peladas": confere o outfit de novo no próximo frame
            auto& self = Jarl::Get();
            if (const auto a = RE::TESForm::LookupByID<RE::Actor>(npcId)) {
                EnsureOutfit(a, self.outfitBase_);
            }
        });

        Government::Get().AssignGovernor(a_faction, province, npcId);  // atualiza a vaga com o FormID real
        {
            std::scoped_lock l{ lock_ };
            byProvince_[province]  = { npcId };
            npcToProvince_[npcId]  = province;
        }
        logger::info("Governo local: governador 0x{:08X} ('{}') colocado em '{}' (província '{}').", npcId,
                     actor->GetName(), t->key, Config::Get().NameOf(province));
        I18n::NotifyTr("jarl.placed", std::string(Config::Get().NameOf(province)), t->name);
    }

    void Jarl::Remove(Id a_province)
    {
        RE::FormID npc{};
        {
            std::scoped_lock l{ lock_ };
            const auto       it = byProvince_.find(a_province);
            if (it == byProvince_.end()) {
                return;
            }
            npc = it->second.npc;
            byProvince_.erase(it);
            npcToProvince_.erase(npc);
        }
        if (npc != 0) {
            if (const auto ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(npc); ref && !ref->IsDeleted()) {
                ref->Disable();
                ref->SetDelete(true);
            }
            logger::info("Governo local: governador de '{}' removido (NPC 0x{:08X}).", Config::Get().NameOf(a_province),
                         npc);
            I18n::NotifyTr("jarl.removed", std::string(Config::Get().NameOf(a_province)));
        }
    }

    void Jarl::OnGovernorsLost(const std::vector<Id>& a_provinces)
    {
        for (const auto p : a_provinces) {
            if (HasNpc(p)) {
                Remove(p);
            }
        }
    }

    void Jarl::OnConquered(const Territory& a_territory, Id, Conquest::Source)
    {
        if (a_territory.province == kNeutral || CapitalOf(a_territory.province) != a_territory.id) {
            return;  // só a capital interessa
        }
        ScopedTimer timer{ "Jarl::OnConquered" };
        const auto  newOwner = State::Get().Owner(a_territory.id);

        // A capital trocou de mãos: se já havia presença nossa ali (de qualquer facção), ela não faz mais
        // sentido fisicamente (o local agora é de outro dono) — remove antes de considerar uma oferta nova.
        if (HasNpc(a_territory.province)) {
            Remove(a_territory.province);
        }
        const auto player = Factions::Get().PlayerFaction();
        if (!player || newOwner != player->id) {
            return;  // presença só para a facção do jogador (ADR 0056)
        }
        Offer(a_territory.id);
    }

    void Jarl::DebugOfferHere()
    {
        auto t = Conquest::Get().TerritoryAtPlayer(/*onlyCleared*/ false);
        if (!t) {
            t = Config::Get().Find(Config::Get().Debug().territory);
        }
        if (!t || t->province == kNeutral) {
            I18n::NotifyTr("jarl.debug.noTerritory");
            return;
        }
        const auto capital = CapitalOf(t->province);
        if (capital == kNeutral) {
            I18n::NotifyTr("jarl.debug.noTerritory");
            return;
        }
        Offer(capital);
    }

    void Jarl::OnActivated(RE::FormID a_npc)
    {
        Id province = kNeutral;
        {
            std::scoped_lock l{ lock_ };
            const auto       it = npcToProvince_.find(a_npc);
            if (it == npcToProvince_.end()) {
                return;  // não é um dos nossos NPCs
            }
            province = it->second;
        }
        double population = 0.0;
        for (const auto& t : Config::Get().Territories()) {
            if (t.province == province) {
                population += Population::Get().Of(t.id);
            }
        }
        const auto name = std::string(Config::Get().NameOf(province));
        const auto body = I18n::Tr("jarl.status", name, std::format("{:.0f}", population),
                                   I18n::Tr("jarl.status.noStability"));  // estabilidade: ponto de extensão (M26)
        if (!Prompt::Ask(body, { I18n::Tr("jarl.ok") }, [](std::size_t) {})) {
            I18n::Notify(body);  // sem MessageBox disponível: notificação simples na tela
        }
    }

    // ---- Cosave -------------------------------------------------------------------------------------

    void Jarl::OnPostLoad()
    {
        std::vector<std::pair<Id, RE::FormID>> gone;
        {
            std::scoped_lock l{ lock_ };
            for (const auto& [province, e] : byProvince_) {
                const auto ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(e.npc);
                if (e.npc == 0 || !ref || ref->IsDeleted()) {
                    gone.emplace_back(province, e.npc);
                }
            }
        }
        if (gone.empty()) {
            return;
        }
        std::scoped_lock l{ lock_ };
        for (const auto& [province, npc] : gone) {
            byProvince_.erase(province);
            npcToProvince_.erase(npc);
            logger::warn("Governo local: NPC 0x{:08X} de '{}' não encontrado ao carregar; presença perdida (TODO: "
                         "recriar na próxima oferta/hotkey).",
                         npc, Config::Get().NameOf(province));
        }
    }

    void Jarl::Clear()
    {
        std::scoped_lock l{ lock_ };
        byProvince_.clear();
        npcToProvince_.clear();
    }

    // Formato do registro 'JARL' v1:  u32 count, depois count × { Id província, FormID npc }.
    void Jarl::Save(SKSE::SerializationInterface* a_intfc) const
    {
        std::scoped_lock l{ lock_ };
        if (byProvince_.empty()) {
            return;
        }
        if (!a_intfc->OpenRecord(kRecord, kRecordVersion)) {
            logger::error("Cosave: falha ao abrir o registro JARL.");
            return;
        }
        a_intfc->WriteRecordData(static_cast<std::uint32_t>(byProvince_.size()));
        for (const auto& [province, e] : byProvince_) {
            a_intfc->WriteRecordData(province);
            a_intfc->WriteRecordData(e.npc);
        }
        logger::info("Cosave: JARL v{} salvo ({} presença(s)).", kRecordVersion, byProvince_.size());
    }

    void Jarl::LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version)
    {
        if (a_version != kRecordVersion) {
            logger::warn("Cosave: versão {} do registro JARL não suportada; presença descartada.", a_version);
            return;
        }
        std::uint32_t count{};
        a_intfc->ReadRecordData(count);
        std::scoped_lock l{ lock_ };
        for (std::uint32_t i = 0; i < count; ++i) {
            Id         province{};
            RE::FormID oldNpc{}, newNpc{};
            a_intfc->ReadRecordData(province);
            a_intfc->ReadRecordData(oldNpc);
            if (Data::Get().ProvinceSize(province) == 0) {
                logger::warn("Cosave: província 0x{:08X} do JARL não existe mais; descartada.", province);
                continue;
            }
            if (oldNpc != 0 && !a_intfc->ResolveFormID(oldNpc, newNpc)) {
                logger::warn("Cosave: NPC do governador de '{}' não existe mais.", Config::Get().NameOf(province));
                newNpc = 0;
            }
            byProvince_[province] = { newNpc };
            if (newNpc != 0) {
                npcToProvince_[newNpc] = province;
            }
        }
        logger::info("Cosave: JARL v{} carregado ({} presença(s)).", a_version, byProvince_.size());
    }
}
