#include "Core/Config.h"
#include "UI/PrismaApi.h"
#include "UI/WarMap.h"
#include "UI/WarMapViews.h"

// M11: mapa tático na view HTML do PrismaUI (ADR 0051). Uma única view, criada na primeira abertura (lazy) e só
// escondida ao fechar (reabrir não recarrega o HTML). Comunicação:
//   C++ -> JS: InteropCall("aosWarMapData", snapshot) e InteropCall("aosWarMapResult", {"ok","text"}).
//   JS -> C++: window.aosWarMapAction(JSON) — listener registrado aqui; o WarMap executa numa task.
namespace AoS::WarMapPrisma
{
    namespace
    {
        constexpr auto kViewsDir = "Data/PrismaUI/views";

        PrismaApi::IVPrismaUI1* api_{ nullptr };
        PrismaApi::View         view_{ 0 };
        bool                    domReady_{ false };
        bool                    open_{ false };
        std::string             pending_;  // snapshot que chegou antes do DOM pronto

        void OnDomReady(PrismaApi::View a_view)
        {
            SKSE::GetTaskInterface()->AddTask([a_view] {
                if (a_view != view_ || !api_) {
                    return;
                }
                domReady_ = true;
                logger::info("Mapa tático: view do PrismaUI pronta.");
                if (!pending_.empty()) {
                    api_->InteropCall(view_, "aosWarMapData", pending_.c_str());
                    pending_.clear();
                }
                if (!open_) {
                    api_->Hide(view_);  // fechou antes de carregar
                }
            });
        }

        void OnAction(const char* a_argument)
        {
            WarMap::Get().HandleViewMessage(a_argument ? std::string(a_argument) : std::string{});
        }
    }

    bool Init()
    {
        api_ = PrismaApi::Request();
        return api_ != nullptr;
    }

    bool Open()
    {
        if (!api_) {
            return false;
        }
        if (view_ == 0 || !api_->IsValid(view_)) {
            const auto& path = Config::Get().WarMap().view;
            std::error_code ec;
            if (!fs::exists(fs::path(kViewsDir) / path, ec)) {
                logger::warn("Mapa tático: view '{}/{}' não encontrada.", kViewsDir, path);
                return false;
            }
            domReady_ = false;
            view_     = api_->CreateView(path.c_str(), OnDomReady);
            if (view_ == 0) {
                logger::warn("Mapa tático: PrismaUI recusou criar a view '{}'.", path);
                return false;
            }
            api_->RegisterJSListener(view_, "aosWarMapAction", OnAction);
            logger::info("Mapa tático: view '{}' criada.", path);
        }
        api_->Show(view_);
        if (!api_->Focus(view_, Config::Get().WarMap().pauseGame)) {
            logger::warn("Mapa tático: PrismaUI não deu foco à view (outra view com foco?).");
        }
        open_ = true;
        return true;
    }

    void Close()
    {
        if (!api_ || view_ == 0 || !open_) {
            return;
        }
        open_ = false;
        api_->Unfocus(view_);
        api_->Hide(view_);
        logger::info("Mapa tático: fechado.");
    }

    bool IsOpen() { return api_ && view_ != 0 && open_; }

    void PushData(const std::string& a_json)
    {
        if (!api_ || view_ == 0) {
            return;
        }
        if (!domReady_) {
            pending_ = a_json;
            return;
        }
        api_->InteropCall(view_, "aosWarMapData", a_json.c_str());
    }

    void PushResult(const std::string& a_json)
    {
        if (api_ && view_ != 0 && domReady_) {
            api_->InteropCall(view_, "aosWarMapResult", a_json.c_str());
        }
    }
}
