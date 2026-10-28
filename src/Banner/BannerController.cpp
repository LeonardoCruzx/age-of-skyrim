#include "Banner/BannerController.h"

#include "Api/Events.h"
#include "Core/Timer.h"
#include "Realm/Realm.h"
#include "Sim/Law.h"  // M25: facção de crime vanilla (bounty), ao lado do gancho do Realm

namespace AoS
{
    BannerController& BannerController::Get() noexcept
    {
        static BannerController instance;
        return instance;
    }

    void BannerController::SetEnabled(RE::TESObjectREFR* a_ref, bool a_enabled)
    {
        if (!a_ref || a_ref->IsDeleted()) {
            return;
        }
        // Só mexe quando o estado difere: evita trabalho e re-disparo de fade/OnLoad à toa.
        if (a_enabled && a_ref->IsDisabled()) {
            a_ref->Enable(false);
        } else if (!a_enabled && !a_ref->IsDisabled()) {
            a_ref->Disable();
        }
    }

    void BannerController::ApplyTerritory(Id a_territory)
    {
        const auto* territory = Config::Get().Find(a_territory);
        if (!territory || territory->banners.empty()) {
            return;  // territórios descobertos ainda não têm bandeiras: custo zero no ApplyAll
        }

        // Bandeiras são por estilo; o dono é uma facção. Neutro -> estilo neutro -> nada habilitado.
        const Id owner    = Factions::Get().StyleOf(State::Get().Owner(a_territory));
        bool     complete = true;

        // Passo 1: desabilita tudo que NÃO é do dono (nunca há duas bandeiras ao mesmo tempo).
        for (const auto& set : territory->banners) {
            if (set.style != owner) {
                complete &= ForEachBanner(set, [](RE::TESObjectREFR* r) { SetEnabled(r, false); });
            }
        }
        // Passo 2: habilita as do dono (se neutro, nenhum conjunto casa e nada é habilitado).
        for (const auto& set : territory->banners) {
            if (set.style == owner) {
                complete &= ForEachBanner(set, [](RE::TESObjectREFR* r) { SetEnabled(r, true); });
            }
        }

        if (complete) {
            pending_.erase(a_territory);
        } else {
            pending_.insert(a_territory);
            logger::warn("Território '{}': referências de banner ainda não resolvidas; tentará de novo ao carregar células.",
                         territory->key);
        }
    }

    void BannerController::ApplyAll()
    {
        ScopedTimer t{ "BannerController::ApplyAll" };
        for (const auto& t : Config::Get().Territories()) {
            ApplyTerritory(t.id);
        }
    }

    void BannerController::RetryPending()
    {
        if (pending_.empty()) {
            return;  // caminho quente: custo zero na maior parte do tempo
        }
        const auto copy = pending_;  // ApplyTerritory mexe em pending_
        for (const auto id : copy) {
            ApplyTerritory(id);
        }
    }

    bool SetTerritoryOwner(Id a_territory, Id a_owner)
    {
        const auto& cfg = Config::Get();
        const auto* t   = cfg.Find(a_territory);
        if (!t) {
            logger::warn("SetTerritoryOwner: território 0x{:08X} desconhecido.", a_territory);
            return false;
        }
        if (!Factions::Get().IsValidOwner(a_owner)) {
            logger::warn("SetTerritoryOwner: facção 0x{:08X} desconhecida.", a_owner);
            return false;
        }

        const auto previous = State::Get().Owner(a_territory);
        const bool changed  = State::Get().SetOwner(a_territory, a_owner);
        logger::info("Território '{}' -> {}", t->key, Factions::Get().NameOf(a_owner));
        if (changed) {
            Api::Events::TerritoryChanged(a_territory, a_owner, previous);  // outros mods (C++ e Papyrus)
        }

        // M18/M19/M22: população (guerra), governadores e títulos reagem só a troca real de dono.
        if (changed) {
            Realm::OnTerritoryOwnerChanged(a_territory, previous, a_owner);
        }
        // M25: facção de crime vanilla do Hold (bounty), ao lado do gancho acima (mesmo evento, sem concorrer).
        if (changed) {
            Law::Get().OnTerritoryOwnerChanged(a_territory, previous, a_owner);
        }

        SKSE::GetTaskInterface()->AddTask([a_territory] { BannerController::Get().ApplyTerritory(a_territory); });
        return true;
    }
}
