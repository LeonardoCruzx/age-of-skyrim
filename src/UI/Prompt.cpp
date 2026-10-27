#include "UI/Prompt.h"

#include "Core/I18n.h"

namespace AoS::Prompt
{
    namespace
    {
        class Callback final : public RE::IMessageBoxCallback
        {
        public:
            explicit Callback(std::function<void(std::size_t)> a_fn) :
                fn_(std::move(a_fn))
            {}
            ~Callback() override = default;

            // O jogo passa 4 + índice do botão (padrão usado por outros plugins CommonLibSSE-NG); o valor bruto
            // vai para o log para conferir no teste. A ação roda numa task (fora do processamento da UI).
            void Run(Message a_msg) override
            {
                const auto raw   = static_cast<std::int32_t>(a_msg);
                const auto index = raw >= 4 ? static_cast<std::size_t>(raw - 4) : kNoChoice;
                logger::info("Prompt: resposta bruta {} -> botão {}.", raw, raw >= 4 ? raw - 4 : -1);
                if (fn_) {
                    SKSE::GetTaskInterface()->AddTask([fn = std::exchange(fn_, nullptr), index] { fn(index); });
                }
            }

        private:
            std::function<void(std::size_t)> fn_;
        };
    }

    bool Ask(const std::string& a_body, const std::vector<std::string>& a_buttons,
             std::function<void(std::size_t)> a_onChoice)
    {
        const auto factories = RE::MessageDataFactoryManager::GetSingleton();
        const auto strings   = RE::InterfaceStrings::GetSingleton();
        const auto creator   = factories && strings ?
                                   factories->GetCreator<RE::MessageBoxData>(strings->messageBoxData) :
                                   nullptr;
        const auto box = creator ? creator->Create() : nullptr;
        if (!box) {
            logger::warn("Prompt: MessageBoxData indisponível; usando o comportamento padrão.");
            return false;
        }
        const auto body = I18n::ToHud(a_body);
        box->bodyText   = body.c_str();
        for (const auto& b : a_buttons) {
            const auto text = I18n::ToHud(b);
            box->buttonText.push_back(RE::BSString{ text.c_str() });
        }
        box->callback = RE::make_smart<Callback>(std::move(a_onChoice));
        box->QueueMessage();
        logger::info("[tela] (pergunta) {}", a_body);
        return true;
    }
}
