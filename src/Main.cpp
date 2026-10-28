#include "Api/Events.h"
#include "Api/ModApi.h"
#include "Banner/BannerController.h"
#include "Core/Factions.h"
#include "Debug/SelfTest.h"
#include "Events/Events.h"
#include "Sim/Economy.h"  // M12
#include "Sim/Routes.h"   // M13
#include "Realm/Government.h"
#include "Realm/Population.h"
#include "Realm/Realm.h"
#include "Realm/Titles.h"
#include "Papyrus/Api.h"
#include "Stock/Stock.h"
#include "World/Claims.h"
#include "World/Conquest.h"
#include "World/Placement.h"
#include "World/Seat.h"

namespace
{
    constexpr std::uint32_t kCosaveUniqueId = 0x414F5331;  // 'AOS1'

    void SetupLog()
    {
        auto path = SKSE::log::log_directory();
        if (!path) {
            return;
        }
        *path /= "AgeOfSkyrim.log";

        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
        auto log  = std::make_shared<spdlog::logger>("aos", std::move(sink));
        log->set_level(spdlog::level::info);
        log->flush_on(spdlog::level::info);
        spdlog::set_default_logger(std::move(log));
        spdlog::set_pattern("[%H:%M:%S.%e] [%l] %v");
    }

    // ---- Cosave ------------------------------------------------------------------------------------
    void OnSave(SKSE::SerializationInterface* a_intfc)
    {
        // Ordem importa no load: FACT (facções) -> CLAM (claims viram territórios dinâmicos) -> TERR (donos).
        AoS::Factions::Get().Save(a_intfc);
        AoS::Claims::Get().Save(a_intfc);
        AoS::State::Get().Save(a_intfc);
        AoS::Placement::Get().Save(a_intfc);
        AoS::SelfTest::Save(a_intfc);
        // ---- M12/M13 ----
        AoS::Economy::Get().Save(a_intfc);
        AoS::Routes::Get().Save(a_intfc);
        // ---- fim M12/M13 ----
        AoS::Seat::Get().Save(a_intfc);  // M6
        AoS::Realm::Government::Get().Save(a_intfc);  // depois do FACT: facções validadas ao carregar
        AoS::Realm::Population::Get().Save(a_intfc);
        AoS::Realm::Titles::Get().Save(a_intfc);
    }

    // Um único laço lê todos os registros e despacha por tipo (cada módulo só lê o seu).
    void OnLoad(SKSE::SerializationInterface* a_intfc)
    {
        std::uint32_t type{}, version{}, length{};
        while (a_intfc->GetNextRecordInfo(type, version, length)) {
            switch (type) {
            case AoS::Factions::kRecord:
                AoS::Factions::Get().LoadRecord(a_intfc, version);
                break;
            case AoS::State::kRecord:
                AoS::State::Get().LoadRecord(a_intfc, version);
                break;
            case AoS::Placement::kRecord:
                AoS::Placement::Get().LoadRecord(a_intfc, version);
                break;
            case AoS::SelfTest::kRecord:
                AoS::SelfTest::LoadRecord(a_intfc, version);
                break;
            case AoS::Claims::kRecord:
                AoS::Claims::Get().LoadRecord(a_intfc, version);
                break;
            // ---- M12/M13 ----
            case AoS::Economy::kRecord:
                AoS::Economy::Get().LoadRecord(a_intfc, version);
                break;
            case AoS::Routes::kRecord:
                AoS::Routes::Get().LoadRecord(a_intfc, version);
                break;
            // ---- fim M12/M13 ----
            case AoS::Seat::kRecord:  // M6
                AoS::Seat::Get().LoadRecord(a_intfc, version);
                break;
            case AoS::Realm::Government::kRecord:
                AoS::Realm::Government::Get().LoadRecord(a_intfc, version);
                break;
            case AoS::Realm::Population::kRecord:
                AoS::Realm::Population::Get().LoadRecord(a_intfc, version);
                break;
            case AoS::Realm::Titles::kRecord:
                AoS::Realm::Titles::Get().LoadRecord(a_intfc, version);
                break;
            default:
                logger::warn("Cosave: registro desconhecido 0x{:08X} ({} bytes) ignorado.", type, length);
                break;
            }
        }
    }

    void OnRevert(SKSE::SerializationInterface*)
    {
        AoS::Claims::Get().Clear();  // antes do State: remove os territórios dinâmicos (M8)
        AoS::Seat::Get().Clear();    // M6
        AoS::Factions::Get().ClearPlayerFactions();
        AoS::State::Get().ResetToDefaults();
        AoS::Placement::Get().Clear();
        AoS::SelfTest::Clear();
        // ---- M12/M13 ----
        AoS::Economy::Get().Clear();
        AoS::Routes::Get().Clear();
        // ---- fim M12/M13 ----
        AoS::Realm::Clear();
        AoS::Api::Events::ClearSession();
    }

    // ---- Mensagens do SKSE ---------------------------------------------------------------------------
    void OnMessage(SKSE::MessagingInterface::Message* a_msg)
    {
        switch (a_msg->type) {
        case SKSE::MessagingInterface::kDataLoaded:
            if (!AoS::Config::Get().Load()) {
                logger::warn("Nenhum território carregado; verifique Data/SKSE/Plugins/AgeOfSkyrim/Territories/.");
            }
            AoS::Factions::Get().Load();  // depois da Config: valida estilos
            AoS::State::Get().ResetToDefaults();
            AoS::Stock::Get().Load();
            AoS::Economy::Get().Load();  // M12/M13: depois do Stock (categorias); configura as rotas
            AoS::Conquest::Get().Register();  // M5
            AoS::Seat::Get().Load();          // M6: depois de Config/Factions/Stock
            AoS::Realm::Load();  // depois de Config + Factions (províncias, idioma)
            // Reino <- economia: comida suficiente = facção sem déficit de manutenção (M12).
            // A troca de doutrina segue grátis até existir pagamento por categoria do baú (ADR 0039).
            AoS::Realm::Population::Get().SetFoodProvider(
                [](AoS::Id a_faction) { return !AoS::Economy::Get().InDeficit(a_faction); });

            // API (M9) <- módulos que ela ainda não conhecia quando foi escrita: sede, economia, rotas, governo.
            AoS::Api::SetSeatProvider([](AoS::Id a_faction) -> std::optional<AgeOfSkyrimAPI::SeatInfo> {
                if (!AoS::Seat::Get().HasSeat() ||
                    AoS::State::Get().Owner(AoS::Seat::Get().SeatTerritory()) != a_faction) {
                    return std::nullopt;  // a sede é de outra facção (ou não existe)
                }
                AgeOfSkyrimAPI::SeatInfo info{};
                info.territory      = AoS::Seat::Get().SeatTerritory();
                info.chestFormID    = AoS::Seat::Get().ChestId();
                info.warTableFormID = AoS::Seat::Get().WarTableId();
                return info;
            });
            AoS::Api::SetTreasuryProvider([](AoS::Id a_faction) -> std::int64_t {
                return static_cast<std::int64_t>(AoS::Economy::Get().Balance(a_faction, AoS::MakeId("gold")));
            });
            AoS::Api::SetTradeRouteProvider([](AoS::Id a_faction) -> std::uint32_t {
                std::uint32_t n = 0;
                for (const auto& r : AoS::Routes::Get().List()) {
                    if (AoS::State::Get().Owner(r.a) == a_faction || AoS::State::Get().Owner(r.b) == a_faction) {
                        ++n;
                    }
                }
                return n;
            });
            AoS::Api::SetGovernmentProvider(
                [](AoS::Id a_faction) -> AoS::Id { return AoS::Realm::Government::Get().DoctrineOf(a_faction); });

            AoS::Events::Register();
            break;

        case SKSE::MessagingInterface::kNewGame:
        case SKSE::MessagingInterface::kPostLoadGame:
            // Jogo novo pode não passar pelo revert: zera todo o estado da sessão anterior (sede, claims, economia...).
            if (a_msg->type == SKSE::MessagingInterface::kNewGame) {
                OnRevert(nullptr);
            }
            // Estado (padrão ou do cosave) -> mundo. Só altera refs cujo estado difere.
            SKSE::GetTaskInterface()->AddTask([] {
                AoS::BannerController::Get().ApplyAll();
                AoS::SelfTest::OnPostLoad();  // só age se o save tem registro do autoteste
                AoS::Seat::Get().OnPostLoad();  // M6: observa e reconta o baú da sede
                AoS::Api::Events::StateReady();  // outros mods: estado pronto para consulta
            });
            break;

        default:
            break;
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse);
    SetupLog();
    logger::info("Age of Skyrim iniciando.");

    SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
    AoS::Api::RegisterMessaging();  // outros plugins pedem a interface (include/AgeOfSkyrimAPI.h)
    AoS::Papyrus::Register();       // natives do AgeOfSkyrim.psc (MCM e mods só-script)

    const auto ser = SKSE::GetSerializationInterface();
    ser->SetUniqueID(kCosaveUniqueId);
    ser->SetSaveCallback(OnSave);
    ser->SetLoadCallback(OnLoad);
    ser->SetRevertCallback(OnRevert);

    return true;
}
