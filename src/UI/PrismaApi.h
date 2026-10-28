#pragma once

// Interface MÍNIMA do PrismaUI (framework de UI web para SKSE), escrita a partir da documentação pública
// (https://www.prismaui.dev/api/...) e da lista de métodos do header público PrismaUI_API.h
// (github.com/PrismaUI-SKSE/example-skse-plugin). Não é cópia do header: só as declarações necessárias para a
// ABI (ordem da vtable, tipos dos callbacks e a função exportada). Ver ADR 0051.
//  - O DLL do PrismaUI exporta `void* RequestPluginAPI(InterfaceVersion)`; o ponteiro devolvido é um objeto com a
//    vtable abaixo (V1 = 21 métodos virtuais puros, nesta ordem). Se o PrismaUI mudar a V1 (não deveria: as
//    versões novas viram V2, V3...), basta trocar esta declaração.
//  - Sem PrismaUI instalado, GetModuleHandle falha e o mod usa os menus vanilla (UI/WarMapMenus).
namespace AoS::PrismaApi
{
    using View = std::uint64_t;  // 0 = inválida

    enum class InterfaceVersion : std::uint8_t
    {
        kV1 = 0,
        kV2 = 1,
    };

    using OnDomReadyCallback = void (*)(View a_view);
    using JSCallback         = void (*)(const char* a_result);
    using JSListenerCallback = void (*)(const char* a_argument);

    // Ordem e assinaturas IGUAIS às do IVPrismaUI1 público (a ordem define a vtable).
    class IVPrismaUI1
    {
    public:
        virtual View CreateView(const char* a_htmlPath, OnDomReadyCallback a_onDomReady = nullptr) noexcept = 0;
        virtual void Invoke(View a_view, const char* a_script, JSCallback a_callback = nullptr) noexcept = 0;
        virtual void InteropCall(View a_view, const char* a_functionName, const char* a_argument) noexcept = 0;
        virtual void RegisterJSListener(View a_view, const char* a_functionName, JSListenerCallback a_callback) noexcept = 0;
        virtual bool HasFocus(View a_view) noexcept = 0;
        virtual bool Focus(View a_view, bool a_pauseGame = false, bool a_disableFocusMenu = false) noexcept = 0;
        virtual void Unfocus(View a_view) noexcept = 0;
        virtual void Show(View a_view) noexcept = 0;
        virtual void Hide(View a_view) noexcept = 0;
        virtual bool IsHidden(View a_view) noexcept = 0;
        virtual int  GetScrollingPixelSize(View a_view) noexcept = 0;
        virtual void SetScrollingPixelSize(View a_view, int a_pixelSize) noexcept = 0;
        virtual bool IsValid(View a_view) noexcept = 0;
        virtual void Destroy(View a_view) noexcept = 0;
        virtual void SetOrder(View a_view, int a_order) noexcept = 0;
        virtual int  GetOrder(View a_view) noexcept = 0;
        virtual void CreateInspectorView(View a_view) noexcept = 0;
        virtual void SetInspectorVisibility(View a_view, bool a_visible) noexcept = 0;
        virtual bool IsInspectorVisible(View a_view) noexcept = 0;
        virtual void SetInspectorBounds(View a_view, float a_topLeftX, float a_topLeftY, unsigned int a_width,
                                        unsigned int a_height) noexcept = 0;
        virtual bool HasAnyActiveFocus() noexcept = 0;

    protected:
        ~IVPrismaUI1() = default;  // o objeto pertence ao PrismaUI; nunca destruir por aqui
    };

    // nullptr se o PrismaUI.dll não está carregado ou não exporta RequestPluginAPI.
    // Chamar em kPostLoad ou depois (o PrismaUI precisa ter sido carregado pelo SKSE).
    [[nodiscard]] inline IVPrismaUI1* Request() noexcept
    {
        using RequestFn    = void* (*)(InterfaceVersion);
        const auto module = REX::W32::GetModuleHandleW(L"PrismaUI.dll");
        if (!module) {
            return nullptr;
        }
        const auto fn = reinterpret_cast<RequestFn>(REX::W32::GetProcAddress(module, "RequestPluginAPI"));
        return fn ? static_cast<IVPrismaUI1*>(fn(InterfaceVersion::kV1)) : nullptr;
    }
}
