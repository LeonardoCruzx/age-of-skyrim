#include "Debug/SelfTest.h"

#include "Banner/BannerController.h"
#include "Core/Factions.h"
#include "Core/I18n.h"
#include "Core/Timer.h"

namespace AoS::SelfTest
{
    namespace
    {
        // Dono deixado pelo último Run(); gravado no cosave para o teste de persistência.
        struct Expectation
        {
            Id territory{ kNeutral };
            Id owner{ kNeutral };  // facção do jogador criada pelo autoteste
        };

        std::optional<Expectation> expected_;  // estado atual (vai para o próximo save)
        std::optional<Expectation> loaded_;    // veio do save que acabou de carregar

        struct Report
        {
            int passed{ 0 };
            int failed{ 0 };

            void Check(bool a_ok, std::string_view a_what)
            {
                (a_ok ? passed : failed)++;
                if (a_ok) {
                    logger::info("[autoteste] PASS  {}", a_what);
                } else {
                    logger::error("[autoteste] FAIL  {}", a_what);
                }
            }
        };

        // Confere que só as bandeiras do dono estão habilitadas. Retorna quantas refs foram vistas.
        std::size_t CheckVisible(Report& a_report, const Territory& a_t, Id a_owner)
        {
            const auto& cfg      = Config::Get();
            const auto& factions = Factions::Get();
            const auto  style    = factions.StyleOf(a_owner);
            std::size_t seen     = 0;
            for (const auto& set : a_t.banners) {
                const bool shouldShow = set.style == style;
                ForEachBanner(set, [&](RE::TESObjectREFR* r) {
                    ++seen;
                    a_report.Check(r->IsDisabled() != shouldShow,
                                   std::format("dono={} -> ref 0x{:08X} ({}) {}", factions.NameOf(a_owner),
                                               r->GetFormID(), cfg.NameOf(set.style), shouldShow ? "visível" : "oculta"));
                });
            }
            return seen;
        }
    }

    void Run()
    {
        ScopedTimer timer{ "SelfTest::Run" };
        const auto& cfg = Config::Get();
        const auto* t   = cfg.Find(cfg.Debug().territory);
        if (!t) {
            I18n::NotifyTr("selftest.noTerritory");
            return;
        }
        logger::info("[autoteste] ===== início ('{}') =====", t->key);

        Report report;

        // 1. Objetos de teste existem (cria os que faltam perto do jogador).
        const auto failedPlacements = Placement::Get().EnsurePlaced(*t);
        report.Check(failedPlacements == 0, std::format("criação de objetos ({} falha(s))", failedPlacements));

        // 2. Todas as bandeiras resolvem; loga cada uma (FormID + nome do objeto base).
        std::size_t total = 0;
        bool        complete = true;
        for (const auto& set : t->banners) {
            complete &= ForEachBanner(set, [&](RE::TESObjectREFR* r) {
                ++total;
                const auto base = r->GetBaseObject();
                logger::info("[autoteste] banner {} -> ref 0x{:08X}, base '{}'", cfg.NameOf(set.style), r->GetFormID(),
                             base ? base->GetName() : "?");
            });
        }
        report.Check(complete && total > 0, std::format("{} bandeira(s) resolvida(s)", total));

        // 3. Facção do jogador (cria uma de teste se ainda não existe), com o último estilo declarado.
        auto& factions = Factions::Get();
        auto  player   = factions.PlayerFaction();
        if (!player && !cfg.Styles().empty()) {
            if (const auto id = factions.CreatePlayerFaction(I18n::Tr("selftest.playerFactionName"),
                                                             cfg.Styles().back(), 0x2E7D32)) {
                player = factions.Find(*id);
            }
        }
        report.Check(player.has_value(), "facção do jogador existe");
        report.Check(factions.Ids().size() >= 3, std::format("{} facção(ões) no total", factions.Ids().size()));

        // 4. Cada dono (neutro + todas as facções): só as bandeiras do estilo dele visíveis.
        std::vector<Id> owners{ kNeutral };
        const auto      all = factions.Ids();
        owners.insert(owners.end(), all.begin(), all.end());
        for (const auto owner : owners) {
            State::Get().SetOwner(t->id, owner);
            BannerController::Get().ApplyTerritory(t->id);  // síncrono: já estamos na thread principal
            CheckVisible(report, *t, owner);
        }

        // 5. Deixa a facção do jogador como dona para o teste de persistência (FACT + TERR no cosave).
        const Id finalOwner = player ? player->id : t->defaultOwner;
        State::Get().SetOwner(t->id, finalOwner);
        BannerController::Get().ApplyTerritory(t->id);
        expected_ = Expectation{ t->id, finalOwner };

        logger::info("[autoteste] ===== fim: {} PASS, {} FAIL =====", report.passed, report.failed);
        if (report.failed == 0) {
            I18n::NotifyTr("selftest.ok", report.passed);
        } else {
            I18n::NotifyTr("selftest.failed", report.failed, report.passed + report.failed);
        }
        I18n::NotifyTr("selftest.accents");  // confere se o HUD exibe acentos (utf8 vs cp1252)
    }

    void OnPostLoad()
    {
        if (!loaded_) {
            return;
        }
        const auto  exp = *loaded_;
        loaded_.reset();

        const auto& cfg = Config::Get();
        const auto* t   = cfg.Find(exp.territory);
        if (!t) {
            return;
        }
        logger::info("[autoteste] ===== persistência ('{}', esperado: {}) =====", t->key,
                     Factions::Get().NameOf(exp.owner));

        Report     report;
        const auto faction = Factions::Get().Find(exp.owner);
        report.Check(faction && faction->player, "facção do jogador restaurada do cosave");
        report.Check(State::Get().Owner(exp.territory) == exp.owner, "dono restaurado do cosave");
        const auto seen = CheckVisible(report, *t, exp.owner);
        report.Check(seen > 0, std::format("{} objeto(s) de teste encontrado(s) após carregar", seen));

        logger::info("[autoteste] ===== persistência: {} PASS, {} FAIL =====", report.passed, report.failed);
        if (report.failed == 0) {
            I18n::NotifyTr("selftest.persistOk");
        } else {
            I18n::NotifyTr("selftest.persistFailed", report.failed);
        }
    }

    void Clear()
    {
        expected_.reset();
        loaded_.reset();
    }

    // Formato do registro 'TEST' v2:  u32 territoryId, u32 factionId (v1 era de antes das facções: ignorado).
    void Save(SKSE::SerializationInterface* a_intfc)
    {
        if (!expected_) {
            return;
        }
        if (a_intfc->OpenRecord(kRecord, kRecordVersion)) {
            a_intfc->WriteRecordData(expected_->territory);
            a_intfc->WriteRecordData(expected_->owner);
        }
    }

    void LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version)
    {
        if (a_version != kRecordVersion) {
            return;
        }
        Expectation e;
        a_intfc->ReadRecordData(e.territory);
        a_intfc->ReadRecordData(e.owner);
        expected_ = e;
        loaded_   = e;
    }
}
