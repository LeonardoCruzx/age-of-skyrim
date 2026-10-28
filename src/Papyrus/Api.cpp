#include "Papyrus/Api.h"

#include "AgeOfSkyrimAPI.h"
#include "Api/Events.h"
#include "Api/ModApi.h"
#include "Core/Config.h"
#include "Core/Factions.h"
#include "Core/I18n.h"
#include "Core/State.h"
#include "Core/Timer.h"
#include "Debug/SelfTest.h"
#include "Realm/Realm.h"  // M27: HasDominated
#include "Stock/Stock.h"
#include "World/Claims.h"

namespace AoS::Papyrus
{
    namespace
    {
        using StaticTag = RE::StaticFunctionTag;

        constexpr auto        kClass       = "AgeOfSkyrim"sv;
        constexpr std::size_t kMaxArray    = 128;  // limite de arrays do Papyrus

        // ---- Auxiliares -----------------------------------------------------------------------------
        Id KeyToId(const std::string& a_key) { return a_key.empty() ? kNeutral : MakeId(a_key); }

        std::string FactionKey(Id a_id)
        {
            if (a_id == kNeutral) {
                return {};
            }
            const auto f = Factions::Get().Find(a_id);
            return f ? f->key : std::string{};
        }

        std::int32_t Clamp32(std::int64_t a_value)
        {
            return static_cast<std::int32_t>(std::clamp<std::int64_t>(a_value, std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max()));
        }

        std::vector<std::int32_t> TotalsToArray(const Stock::Totals& a_totals)
        {
            std::vector<std::int32_t> out;
            out.reserve(a_totals.perCategory.size() + 1);
            for (const auto v : a_totals.perCategory) {
                out.push_back(Clamp32(v));
            }
            out.push_back(Clamp32(a_totals.unclassified));  // último = sem categoria
            return out;
        }

        // Territórios ordenados por nome para a UI (Config não muda depois de kDataLoaded).
        const std::vector<Id>& SortedTerritories()
        {
            static std::once_flag   once;
            static std::vector<Id>  sorted;
            std::call_once(once, [] {
                ScopedTimer t{ "Papyrus: ordenar territórios" };
                std::vector<const Territory*> list;
                for (const auto& tt : Config::Get().Territories()) {
                    list.push_back(&tt);
                }
                std::ranges::sort(list, [](const Territory* a, const Territory* b) { return a->name < b->name; });
                sorted.reserve(list.size());
                for (const auto* tt : list) {
                    sorted.push_back(tt->id);
                }
            });
            return sorted;
        }

        // ---- Geral ----------------------------------------------------------------------------------
        std::int32_t GetApiVersion(StaticTag*) { return static_cast<std::int32_t>(AgeOfSkyrimAPI::kInterfaceVersion); }
        std::string  GetPluginVersion(StaticTag*) { return AOS_VERSION; }

        std::string Tr(StaticTag*, std::string a_key) { return I18n::ForGame(std::string(I18n::Raw(a_key))); }

        std::string Tr1(StaticTag*, std::string a_key, std::string a_arg1)
        {
            return I18n::ForGame(I18n::Tr(a_key, I18n::FromGame(a_arg1)));
        }

        std::string Tr2(StaticTag*, std::string a_key, std::string a_arg1, std::string a_arg2)
        {
            return I18n::ForGame(I18n::Tr(a_key, I18n::FromGame(a_arg1), I18n::FromGame(a_arg2)));
        }

        bool IsDebugEnabled(StaticTag*) { return Config::Get().Debug().enabled; }

        // "owner" (F10), "selftest" (F11), "stock" (F8), "claim" (F9). -1 = nome desconhecido.
        std::int32_t GetDebugHotkey(StaticTag*, std::string a_which)
        {
            const auto& d = Config::Get().Debug();
            if (a_which == "owner") {
                return d.hotkey;
            }
            if (a_which == "selftest") {
                return d.selfTestHotkey;
            }
            if (a_which == "stock") {
                return d.stockHotkey;
            }
            if (a_which == "claim") {
                return d.claimHotkey;
            }
            return -1;
        }

        // ---- Facções --------------------------------------------------------------------------------
        std::vector<std::string> GetFactions(StaticTag*)
        {
            std::vector<std::string> out;
            for (const auto& f : Factions::Get().All()) {
                if (out.size() == kMaxArray) {
                    break;
                }
                out.push_back(f.key);
            }
            return out;
        }

        std::string GetFactionName(StaticTag*, std::string a_key)
        {
            if (a_key.empty()) {
                return I18n::ForGame(std::string(I18n::Raw("faction.neutral")));
            }
            const auto f = Factions::Get().Find(MakeId(a_key));
            return f ? I18n::ForGame(f->name) : std::string{};
        }

        std::string GetFactionStyle(StaticTag*, std::string a_key)
        {
            const auto f = Factions::Get().Find(KeyToId(a_key));
            return f ? std::string(Config::Get().NameOf(f->style)) : std::string{};
        }

        std::int32_t GetFactionColor(StaticTag*, std::string a_key)
        {
            const auto f = Factions::Get().Find(KeyToId(a_key));
            return f ? static_cast<std::int32_t>(f->color) : -1;
        }

        bool IsPlayerFaction(StaticTag*, std::string a_key)
        {
            const auto f = Factions::Get().Find(KeyToId(a_key));
            return f && f->player;
        }

        std::string GetPlayerFaction(StaticTag*)
        {
            const auto f = Factions::Get().PlayerFaction();
            return f ? f->key : std::string{};
        }

        std::string CreatePlayerFaction(StaticTag*, std::string a_name, std::string a_style, std::int32_t a_color)
        {
            auto name = I18n::FromGame(a_name);
            if (name.empty()) {
                return {};
            }
            const auto id = Factions::Get().CreatePlayerFaction(std::move(name), KeyToId(a_style),
                                                                static_cast<std::uint32_t>(a_color));
            return id ? FactionKey(*id) : std::string{};
        }

        bool RenameFaction(StaticTag*, std::string a_key, std::string a_name)
        {
            return Factions::Get().Rename(KeyToId(a_key), I18n::FromGame(a_name));
        }

        bool SetFactionStyle(StaticTag*, std::string a_key, std::string a_style)
        {
            return Factions::Get().SetStyle(KeyToId(a_key), KeyToId(a_style));
        }

        bool SetFactionColor(StaticTag*, std::string a_key, std::int32_t a_color)
        {
            return Factions::Get().SetColor(KeyToId(a_key), static_cast<std::uint32_t>(a_color));
        }

        std::int32_t CountTerritoriesOwnedBy(StaticTag*, std::string a_key)
        {
            const Id     owner = KeyToId(a_key);
            std::int32_t n     = 0;
            for (const auto& t : Config::Get().Territories()) {
                n += State::Get().Owner(t.id) == owner ? 1 : 0;
            }
            return n;
        }

        std::vector<std::string> GetStyles(StaticTag*)
        {
            std::vector<std::string> out;
            for (const auto id : Config::Get().Styles()) {
                if (out.size() == kMaxArray) {
                    break;
                }
                out.push_back(std::string(Config::Get().NameOf(id)));
            }
            return out;
        }

        // Nome traduzido do estilo ("style.<chave>" nas traduções); estilos de outros mods mostram a chave.
        std::string GetStyleName(StaticTag*, std::string a_style)
        {
            const auto text = I18n::Find("style." + a_style);
            return I18n::ForGame(text ? std::string(*text) : a_style);
        }

        // Paralelo a GetStyles (o Papyrus do Skyrim não cria arrays de tamanho variável).
        std::vector<std::string> GetStyleNames(StaticTag* a_tag)
        {
            auto out = GetStyles(a_tag);
            for (auto& s : out) {
                s = GetStyleName(a_tag, s);
            }
            return out;
        }

        // Escolhas de dono para menus: "" (neutro) primeiro, depois todas as facções.
        std::vector<std::string> GetOwnerChoices(StaticTag* a_tag)
        {
            auto out = GetFactions(a_tag);
            out.insert(out.begin(), std::string{});
            if (out.size() > kMaxArray) {
                out.resize(kMaxArray);
            }
            return out;
        }

        std::vector<std::string> GetOwnerChoiceNames(StaticTag* a_tag)
        {
            auto out = GetOwnerChoices(a_tag);
            for (auto& s : out) {
                s = GetFactionName(a_tag, s);
            }
            return out;
        }

        // ---- Territórios ----------------------------------------------------------------------------
        std::int32_t GetTerritoryCount(StaticTag*) { return static_cast<std::int32_t>(Config::Get().Territories().size()); }

        // Página da lista ordenada por nome (no máximo 128 por chamada).
        std::vector<std::string> GetTerritories(StaticTag*, std::int32_t a_offset, std::int32_t a_count)
        {
            const auto&              sorted = SortedTerritories();
            std::vector<std::string> out;
            if (a_offset < 0 || a_count <= 0) {
                return out;
            }
            const auto first = static_cast<std::size_t>(a_offset);
            const auto last  = std::min(sorted.size(), first + std::min<std::size_t>(a_count, kMaxArray));
            for (auto i = first; i < last; ++i) {
                if (const auto* t = Config::Get().Find(sorted[i])) {
                    out.push_back(t->key);
                }
            }
            return out;
        }

        std::string GetTerritoryName(StaticTag*, std::string a_key)
        {
            const auto* t = Config::Get().Find(KeyToId(a_key));
            return t ? I18n::ForGame(t->name) : std::string{};
        }

        std::string GetTerritoryOwner(StaticTag*, std::string a_key) { return FactionKey(State::Get().Owner(KeyToId(a_key))); }

        std::string GetTerritoryPreviousOwner(StaticTag*, std::string a_key)
        {
            return FactionKey(Api::Events::PreviousOwner(KeyToId(a_key)));
        }

        std::string GetTerritoryType(StaticTag*, std::string a_key)
        {
            const auto* t = Config::Get().Find(KeyToId(a_key));
            return t ? std::string(ToString(t->type)) : std::string{};
        }

        std::string GetTerritoryProvince(StaticTag*, std::string a_key)
        {
            const auto* t = Config::Get().Find(KeyToId(a_key));
            return t ? I18n::ForGame(t->provinceName) : std::string{};
        }

        std::string GetTerritoryForLocation(StaticTag*, RE::BGSLocation* a_location)
        {
            const auto* t = a_location ? Config::Get().FindByLocation(a_location) : nullptr;
            return t ? t->key : std::string{};
        }

        bool SetTerritoryOwner(StaticTag*, std::string a_territory, std::string a_faction)
        {
            return Api::ExternalSetOwner(KeyToId(a_territory), KeyToId(a_faction), "Papyrus");
        }

        // ---- Estoque (thread principal) -------------------------------------------------------------
        std::vector<std::string> GetStockCategories(StaticTag*)
        {
            std::vector<std::string> out;
            for (const auto& c : Stock::Get().Categories()) {
                out.push_back(c.key);
            }
            return out;
        }

        std::vector<std::string> GetStockCategoryNames(StaticTag*)
        {
            std::vector<std::string> out;
            for (const auto& c : Stock::Get().Categories()) {
                out.push_back(I18n::ForGame(c.name));
            }
            return out;
        }

        // Contagem agora: um elemento por categoria + o último = itens sem categoria.
        std::vector<std::int32_t> GetContainerStockTotals(StaticTag*, RE::TESObjectREFR* a_container)
        {
            if (!a_container || Stock::Get().Categories().empty()) {
                return {};
            }
            ScopedTimer t{ "Papyrus: GetContainerStockTotals" };
            return TotalsToArray(Stock::Get().CountByCategory(a_container));
        }

        std::vector<std::int32_t> GetPlayerStockTotals(StaticTag* a_tag)
        {
            return GetContainerStockTotals(a_tag, RE::PlayerCharacter::GetSingleton());
        }

        // ---- Ganchos: sede (M6), tesouro (M12), rotas (M13), governo (M18) --------------------------
        bool HasSeat(StaticTag*, std::string a_faction) { return Api::Seat(KeyToId(a_faction)).has_value(); }

        std::string GetSeatTerritory(StaticTag*, std::string a_faction)
        {
            const auto seat = Api::Seat(KeyToId(a_faction));
            const auto* t   = seat ? Config::Get().Find(seat->territory) : nullptr;
            return t ? t->key : std::string{};
        }

        RE::TESObjectREFR* GetSeatChest(StaticTag*, std::string a_faction)
        {
            const auto seat = Api::Seat(KeyToId(a_faction));
            return seat && seat->chestFormID ? RE::TESForm::LookupByID<RE::TESObjectREFR>(seat->chestFormID) : nullptr;
        }

        std::int32_t GetTreasury(StaticTag*, std::string a_faction) { return Clamp32(Api::Treasury(KeyToId(a_faction))); }

        std::int32_t GetTradeRouteCount(StaticTag*, std::string a_faction)
        {
            return static_cast<std::int32_t>(Api::TradeRoutes(KeyToId(a_faction)));
        }

        std::int32_t GetGovernmentId(StaticTag*, std::string a_faction)
        {
            return static_cast<std::int32_t>(Api::Government(KeyToId(a_faction)));
        }

        // M27 (ADR 0057): "Dominador(a) de Skyrim" — marco permanente, consulta O(1).
        bool HasDominated(StaticTag*, std::string a_faction) { return Realm::HasDominated(KeyToId(a_faction)); }

        // ---- Free Claim e ações de debug (thread principal) -----------------------------------------
        std::int32_t GetClaimCount(StaticTag*) { return static_cast<std::int32_t>(Claims::Get().All().size()); }
        std::int32_t GetMaxClaims(StaticTag*) { return Config::Get().Claim().maxClaims; }

        void RunSelfTest(StaticTag*) { SelfTest::Run(); }
        void ShowPlayerStock(StaticTag*) { Stock::Get().ReportPlayerInventory(); }
        void PlantClaim(StaticTag*) { Claims::Get().TryClaimHere(); }

        bool RegisterFunctions(RE::BSScript::IVirtualMachine* a_vm)
        {
            if (!a_vm) {
                return false;
            }
            constexpr bool kAnyThread = true;   // só lê estado com lock / imutável
            constexpr bool kMain      = false;  // inventário, mundo ou alteração de estado

            a_vm->RegisterFunction("GetApiVersion", kClass, GetApiVersion, kAnyThread);
            a_vm->RegisterFunction("GetPluginVersion", kClass, GetPluginVersion, kAnyThread);
            a_vm->RegisterFunction("Tr", kClass, Tr, kAnyThread);
            a_vm->RegisterFunction("Tr1", kClass, Tr1, kAnyThread);
            a_vm->RegisterFunction("Tr2", kClass, Tr2, kAnyThread);
            a_vm->RegisterFunction("IsDebugEnabled", kClass, IsDebugEnabled, kAnyThread);
            a_vm->RegisterFunction("GetDebugHotkey", kClass, GetDebugHotkey, kAnyThread);

            a_vm->RegisterFunction("GetFactions", kClass, GetFactions, kAnyThread);
            a_vm->RegisterFunction("GetFactionName", kClass, GetFactionName, kAnyThread);
            a_vm->RegisterFunction("GetFactionStyle", kClass, GetFactionStyle, kAnyThread);
            a_vm->RegisterFunction("GetFactionColor", kClass, GetFactionColor, kAnyThread);
            a_vm->RegisterFunction("IsPlayerFaction", kClass, IsPlayerFaction, kAnyThread);
            a_vm->RegisterFunction("GetPlayerFaction", kClass, GetPlayerFaction, kAnyThread);
            a_vm->RegisterFunction("CreatePlayerFaction", kClass, CreatePlayerFaction, kMain);
            a_vm->RegisterFunction("RenameFaction", kClass, RenameFaction, kMain);
            a_vm->RegisterFunction("SetFactionStyle", kClass, SetFactionStyle, kMain);
            a_vm->RegisterFunction("SetFactionColor", kClass, SetFactionColor, kMain);
            a_vm->RegisterFunction("CountTerritoriesOwnedBy", kClass, CountTerritoriesOwnedBy, kAnyThread);
            a_vm->RegisterFunction("GetStyles", kClass, GetStyles, kAnyThread);
            a_vm->RegisterFunction("GetStyleName", kClass, GetStyleName, kAnyThread);
            a_vm->RegisterFunction("GetStyleNames", kClass, GetStyleNames, kAnyThread);
            a_vm->RegisterFunction("GetOwnerChoices", kClass, GetOwnerChoices, kAnyThread);
            a_vm->RegisterFunction("GetOwnerChoiceNames", kClass, GetOwnerChoiceNames, kAnyThread);

            a_vm->RegisterFunction("GetTerritoryCount", kClass, GetTerritoryCount, kAnyThread);
            a_vm->RegisterFunction("GetTerritories", kClass, GetTerritories, kAnyThread);
            a_vm->RegisterFunction("GetTerritoryName", kClass, GetTerritoryName, kAnyThread);
            a_vm->RegisterFunction("GetTerritoryOwner", kClass, GetTerritoryOwner, kAnyThread);
            a_vm->RegisterFunction("GetTerritoryPreviousOwner", kClass, GetTerritoryPreviousOwner, kAnyThread);
            a_vm->RegisterFunction("GetTerritoryType", kClass, GetTerritoryType, kAnyThread);
            a_vm->RegisterFunction("GetTerritoryProvince", kClass, GetTerritoryProvince, kAnyThread);
            a_vm->RegisterFunction("GetTerritoryForLocation", kClass, GetTerritoryForLocation, kAnyThread);
            a_vm->RegisterFunction("SetTerritoryOwner", kClass, SetTerritoryOwner, kMain);

            a_vm->RegisterFunction("GetStockCategories", kClass, GetStockCategories, kAnyThread);
            a_vm->RegisterFunction("GetStockCategoryNames", kClass, GetStockCategoryNames, kAnyThread);
            a_vm->RegisterFunction("GetContainerStockTotals", kClass, GetContainerStockTotals, kMain);
            a_vm->RegisterFunction("GetPlayerStockTotals", kClass, GetPlayerStockTotals, kMain);

            a_vm->RegisterFunction("HasSeat", kClass, HasSeat, kMain);
            a_vm->RegisterFunction("GetSeatTerritory", kClass, GetSeatTerritory, kMain);
            a_vm->RegisterFunction("GetSeatChest", kClass, GetSeatChest, kMain);
            a_vm->RegisterFunction("GetTreasury", kClass, GetTreasury, kMain);
            a_vm->RegisterFunction("GetTradeRouteCount", kClass, GetTradeRouteCount, kMain);
            a_vm->RegisterFunction("GetGovernmentId", kClass, GetGovernmentId, kMain);
            a_vm->RegisterFunction("HasDominated", kClass, HasDominated, kAnyThread);  // M27

            a_vm->RegisterFunction("GetClaimCount", kClass, GetClaimCount, kMain);
            a_vm->RegisterFunction("GetMaxClaims", kClass, GetMaxClaims, kAnyThread);
            a_vm->RegisterFunction("RunSelfTest", kClass, RunSelfTest, kMain);
            a_vm->RegisterFunction("ShowPlayerStock", kClass, ShowPlayerStock, kMain);
            a_vm->RegisterFunction("PlantClaim", kClass, PlantClaim, kMain);

            logger::info("Papyrus: natives do script '{}' registrados.", kClass);
            return true;
        }
    }

    void Register()
    {
        if (const auto papyrus = SKSE::GetPapyrusInterface(); !papyrus || !papyrus->Register(RegisterFunctions)) {
            logger::warn("Papyrus: falha ao registrar natives; MCM e scripts de outros mods não funcionarão.");
        }
    }
}
