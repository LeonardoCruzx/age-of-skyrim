#include "Banner/BannerController.h"
#include "Events/Events.h"

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
    void OnSave(SKSE::SerializationInterface* a_intfc) { AoS::State::Get().Save(a_intfc); }
    void OnLoad(SKSE::SerializationInterface* a_intfc) { AoS::State::Get().Load(a_intfc); }
    void OnRevert(SKSE::SerializationInterface*) { AoS::State::Get().ResetToDefaults(); }

    // ---- Mensagens do SKSE ---------------------------------------------------------------------------
    void OnMessage(SKSE::MessagingInterface::Message* a_msg)
    {
        switch (a_msg->type) {
        case SKSE::MessagingInterface::kDataLoaded:
            if (!AoS::Config::Get().Load()) {
                logger::warn("Nenhum território carregado; verifique Data/SKSE/Plugins/AgeOfSkyrim/Territories/.");
            }
            AoS::State::Get().ResetToDefaults();
            AoS::Events::Register();
            break;

        case SKSE::MessagingInterface::kNewGame:
        case SKSE::MessagingInterface::kPostLoadGame:
            // Estado (padrão ou do cosave) -> mundo. Só altera refs cujo estado difere.
            SKSE::GetTaskInterface()->AddTask([] { AoS::BannerController::Get().ApplyAll(); });
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
