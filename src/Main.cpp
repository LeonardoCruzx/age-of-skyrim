#include "Banner/BannerController.h"
#include "Core/Factions.h"
#include "Debug/SelfTest.h"
#include "Events/Events.h"
#include "World/Placement.h"

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
        AoS::Factions::Get().Save(a_intfc);
        AoS::State::Get().Save(a_intfc);
        AoS::Placement::Get().Save(a_intfc);
        AoS::SelfTest::Save(a_intfc);
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
            default:
                logger::warn("Cosave: registro desconhecido 0x{:08X} ({} bytes) ignorado.", type, length);
                break;
            }
        }
    }

    void OnRevert(SKSE::SerializationInterface*)
    {
        AoS::Factions::Get().ClearPlayerFactions();
        AoS::State::Get().ResetToDefaults();
        AoS::Placement::Get().Clear();
        AoS::SelfTest::Clear();
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
            AoS::Events::Register();
            break;

        case SKSE::MessagingInterface::kNewGame:
        case SKSE::MessagingInterface::kPostLoadGame:
            // Estado (padrão ou do cosave) -> mundo. Só altera refs cujo estado difere.
            SKSE::GetTaskInterface()->AddTask([] {
                AoS::BannerController::Get().ApplyAll();
                AoS::SelfTest::OnPostLoad();  // só age se o save tem registro do autoteste
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

    const auto ser = SKSE::GetSerializationInterface();
    ser->SetUniqueID(kCosaveUniqueId);
    ser->SetSaveCallback(OnSave);
    ser->SetLoadCallback(OnLoad);
    ser->SetRevertCallback(OnRevert);

    return true;
}
