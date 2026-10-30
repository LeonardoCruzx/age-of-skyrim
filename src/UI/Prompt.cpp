#include "UI/Prompt.h"

#include "Core/I18n.h"

namespace AoS::Prompt
{
    namespace
    {
        class Callback final : public RE::IMessageBoxCallback
        {
        public:
            Callback(std::function<void(std::size_t)> a_fn, std::size_t a_buttons) :
                fn_(std::move(a_fn)), buttons_(a_buttons)
            {}
            ~Callback() override = default;

            // O valor bruto é o próprio índice do botão (0 = primeiro) — confirmado no jogo (ADR 0071); a
            // suposição antiga de "4 + índice" fazia "Sim" virar recusa e "Fechar" abrir outra opção. Fora da
            // faixa = sem escolha. A ação roda numa task (fora do processamento da UI).
            void Run(Message a_msg) override
            {
                const auto raw   = static_cast<std::int32_t>(a_msg);
                const bool valid = raw >= 0 && static_cast<std::size_t>(raw) < buttons_;
                const auto index = valid ? static_cast<std::size_t>(raw) : kNoChoice;
                logger::info("Prompt: resposta bruta {} -> botão {}.", raw, valid ? raw : -1);
                if (fn_) {
                    SKSE::GetTaskInterface()->AddTask([fn = std::exchange(fn_, nullptr), index] { fn(index); });
                }
            }

        private:
            std::function<void(std::size_t)> fn_;
            std::size_t                      buttons_;
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
        box->callback = RE::make_smart<Callback>(std::move(a_onChoice), a_buttons.size());
        box->QueueMessage();
        logger::info("[tela] (pergunta) {}", a_body);
        return true;
    }
}
