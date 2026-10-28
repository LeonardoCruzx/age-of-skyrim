#pragma once

// =====================================================================================================
//  Age of Skyrim — API pública para outros plugins SKSE (C++)
// =====================================================================================================
//  Copie este arquivo para o seu projeto. Nada aqui depende da CommonLib, a não ser o auxiliar
//  RequestInterface() no fim (só compilado se SKSE/SKSE.h estiver disponível).
//
//  Como obter a interface (no seu plugin):
//
//      #include "AgeOfSkyrimAPI.h"
//      AgeOfSkyrimAPI::IAgeOfSkyrim* g_aos = nullptr;
//
//      void OnMessage(SKSE::MessagingInterface::Message* a_msg) {
//          if (a_msg->type == SKSE::MessagingInterface::kPostLoad) {       // ou kPostPostLoad
//              g_aos = AgeOfSkyrimAPI::RequestInterface();                // nullptr = mod ausente/antigo
//              if (g_aos) {
//                  g_aos->AddListener(AgeOfSkyrimAPI::EventType::kTerritoryChanged, OnAoSEvent, nullptr);
//              }
//          }
//      }
//
//  Regras de uso:
//   - Peça a interface em kPostLoad ou depois (antes disso o Age of Skyrim pode não ter carregado).
//   - Consultas valem a partir de kDataLoaded (territórios e facções são lidos nesse momento); o estado
//     do jogo (donos, facções do jogador) vale depois de carregar um save (evento kStateReady).
//   - Consultas são thread-safe, exceto GetStockTotals (só na thread principal). SetTerritoryOwner pode
//     ser chamado de qualquer thread; o efeito no mundo é agendado na thread principal.
//   - Callbacks de evento são SEMPRE chamados na thread principal, depois que a mudança aconteceu.
//     Não bloqueie dentro deles.
//   - Texto: UTF-8. Funções que devolvem texto copiam para o buffer do chamador (com '\0') e retornam o
//     tamanho total necessário (sem o '\0'); passe a_size = 0 para só descobrir o tamanho.
//   - Ids: hash FNV-1a de 32 bits da chave de texto (minúsculas), 0 = neutro/nenhum. Estáveis entre
//     versões e saves (mesma regra do mod: MakeId abaixo).
//
//  Versionamento: a interface só CRESCE. Funções novas vão no fim da vtable e sobem kInterfaceVersion;
//  nada é removido nem reordenado. Peça a versão mínima de que precisa; GetVersion() diz a real.
// =====================================================================================================

#include <cstddef>
#include <cstdint>

namespace AgeOfSkyrimAPI
{
    inline constexpr const char*   kPluginName       = "AgeOfSkyrim";
    inline constexpr std::uint32_t kInterfaceVersion = 1;

    using FactionId   = std::uint32_t;
    using TerritoryId = std::uint32_t;
    using StyleId     = std::uint32_t;

    inline constexpr std::uint32_t kNone = 0;  // neutro / nenhum

    // Mesmo hash do mod (FNV-1a 32 bits, ASCII case-insensitive; 0 vira 1).
    [[nodiscard]] constexpr std::uint32_t MakeId(const char* a_key) noexcept
    {
        std::uint32_t h = 2166136261u;
        for (; a_key && *a_key; ++a_key) {
            char c = *a_key;
            if (c >= 'A' && c <= 'Z') {
                c = static_cast<char>(c - 'A' + 'a');
            }
            h ^= static_cast<std::uint8_t>(c);
            h *= 16777619u;
        }
        return h == 0 ? 1u : h;
    }

    // ---- Mensagem SKSE para pedir a interface ---------------------------------------------------------
    // Envie com MessagingInterface::Dispatch(kMessageRequestInterface, &req, sizeof(req), kPluginName).
    // O Age of Skyrim responde na hora (Dispatch é síncrono) preenchendo req.result.
    inline constexpr std::uint32_t kMessageRequestInterface = 0x414F5349;  // 'AOSI'

    class IAgeOfSkyrim;

    struct InterfaceRequest
    {
        std::uint32_t requestedVersion{ kInterfaceVersion };
        IAgeOfSkyrim* result{ nullptr };  // nullptr se requestedVersion > versão disponível
    };

    // Alternativa sem mensagens: GetProcAddress(GetModuleHandleW(L"AgeOfSkyrim.dll"), "AoS_GetInterface").
    using GetInterfaceFn = IAgeOfSkyrim* (*)(std::uint32_t a_requestedVersion);

    // ---- Tipos ----------------------------------------------------------------------------------------
    enum class TerritoryType : std::uint8_t
    {
        kUnknown,
        kFort,
        kTower,
        kCamp,
        kVillage,
        kCity,
    };

    struct FactionInfo
    {
        std::uint32_t size{ sizeof(FactionInfo) };  // preenchido pelo chamador; o mod só escreve até size
        FactionId     id{ kNone };
        StyleId       style{ kNone };
        std::uint32_t color{ 0xFFFFFF };  // 0xRRGGBB
        bool          isPlayer{ false };
    };

    struct TerritoryInfo
    {
        std::uint32_t size{ sizeof(TerritoryInfo) };
        TerritoryId   id{ kNone };
        FactionId     owner{ kNone };
        TerritoryType type{ TerritoryType::kUnknown };
        std::uint32_t province{ kNone };        // Id do Hold (0 = desconhecido)
        std::uint32_t locationFormID{ 0 };      // BGSLocation em runtime (0 = sem local)
        std::uint32_t worldspaceFormID{ 0 };
        bool          hasPosition{ false };
        float         position[3]{};            // marcador do mapa (unidades do jogo)
    };

    struct SeatInfo  // M6 — até lá HasSeat() é sempre false
    {
        std::uint32_t size{ sizeof(SeatInfo) };
        TerritoryId   territory{ kNone };
        std::uint32_t chestFormID{ 0 };     // baú de estoque (referência criada em runtime)
        std::uint32_t warTableFormID{ 0 };  // mesa de guerra
    };

    // ---- Eventos --------------------------------------------------------------------------------------
    enum class EventType : std::uint32_t
    {
        kStateReady       = 1,   // estado pronto (novo jogo ou save carregado e aplicado ao mundo)
        kTerritoryChanged = 2,   // territory, faction = novo dono, previousFaction = dono anterior
        kFactionCreated   = 3,   // faction
        kFactionChanged   = 4,   // faction (nome, estilo ou cor)
        kClaimCreated     = 5,   // value = nº de claims, formID = estandarte
        kStockChanged     = 6,   // formID = container recontado (baú da sede a partir da M6)
        kSeatFounded      = 7,   // emitido a partir da M6: faction, territory
        kSeatMoved        = 8,   // emitido a partir da M6: faction, territory (nova), previousTerritory
        kTreasuryChanged  = 9,   // emitido a partir da M12: faction, value = saldo
        kTradeRouteChanged = 10, // emitido a partir da M13: faction, territory, previousTerritory, value = 1/0
        kGovernmentChanged = 11, // emitido a partir da M18: faction
        kPopulationChanged = 12, // emitido a partir da M19: territory, value = população
        kTitleGranted      = 13, // emitido a partir da M22: faction, value = título
    };

    struct Event
    {
        std::uint32_t size{ sizeof(Event) };  // campos novos só no fim; confira size antes de lê-los
        EventType     type{};
        TerritoryId   territory{ kNone };
        TerritoryId   previousTerritory{ kNone };
        FactionId     faction{ kNone };
        FactionId     previousFaction{ kNone };
        std::uint32_t formID{ 0 };
        std::int64_t  value{ 0 };
    };

    using EventCallback = void (*)(const Event& a_event, void* a_user);
    using ListenerHandle = std::uint32_t;  // 0 = falhou

    // ---- Interface ------------------------------------------------------------------------------------
    class IAgeOfSkyrim
    {
    public:
        [[nodiscard]] virtual std::uint32_t GetVersion() const noexcept = 0;           // kInterfaceVersion real
        [[nodiscard]] virtual std::size_t   GetPluginVersion(char* a_buf, std::size_t a_size) const noexcept = 0;

        // Facções (pré-definidas no JSON + do jogador). Retornam o total; copiam até a_capacity.
        [[nodiscard]] virtual std::uint32_t GetFactions(FactionId* a_out, std::uint32_t a_capacity) const noexcept = 0;
        [[nodiscard]] virtual bool          GetFactionInfo(FactionId a_id, FactionInfo* a_out) const noexcept = 0;
        [[nodiscard]] virtual std::size_t   GetFactionKey(FactionId a_id, char* a_buf, std::size_t a_size) const noexcept = 0;
        [[nodiscard]] virtual std::size_t   GetFactionName(FactionId a_id, char* a_buf, std::size_t a_size) const noexcept = 0;
        [[nodiscard]] virtual FactionId     GetPlayerFaction() const noexcept = 0;  // kNone se o jogador não criou

        // Territórios (JSON + auto-descobertos dos BGSLocation).
        [[nodiscard]] virtual std::uint32_t GetTerritories(TerritoryId* a_out, std::uint32_t a_capacity) const noexcept = 0;
        [[nodiscard]] virtual bool          GetTerritoryInfo(TerritoryId a_id, TerritoryInfo* a_out) const noexcept = 0;
        [[nodiscard]] virtual std::size_t   GetTerritoryKey(TerritoryId a_id, char* a_buf, std::size_t a_size) const noexcept = 0;
        [[nodiscard]] virtual std::size_t   GetTerritoryName(TerritoryId a_id, char* a_buf, std::size_t a_size) const noexcept = 0;
        [[nodiscard]] virtual TerritoryId   GetTerritoryByLocation(std::uint32_t a_locationFormID) const noexcept = 0;  // O(1)
        [[nodiscard]] virtual FactionId     GetTerritoryOwner(TerritoryId a_id) const noexcept = 0;
        // Muda o dono (validado; bandeiras trocadas na thread principal). false se território/facção não existe
        // ou se settings.json -> api.allowOwnerChanges = false.
        virtual bool                        SetTerritoryOwner(TerritoryId a_id, FactionId a_owner) noexcept = 0;

        // Estoque por categoria (Resources.json). Índice = ordem das categorias.
        [[nodiscard]] virtual std::uint32_t GetStockCategoryCount() const noexcept = 0;
        [[nodiscard]] virtual std::size_t   GetStockCategoryKey(std::uint32_t a_index, char* a_buf, std::size_t a_size) const noexcept = 0;
        // Última contagem de um container observado (baú da sede). false se nunca foi contado. Thread principal.
        [[nodiscard]] virtual bool          GetStockTotals(std::uint32_t a_containerFormID, std::int64_t* a_out,
                                                           std::uint32_t a_capacity) const noexcept = 0;

        // Sede (M6), tesouro (M12), rotas (M13), governo (M18): já na vtable; devolvem "não disponível"
        // até a missão correspondente (false / 0 / kNone).
        [[nodiscard]] virtual bool          GetSeat(FactionId a_faction, SeatInfo* a_out) const noexcept = 0;
        [[nodiscard]] virtual std::int64_t  GetTreasury(FactionId a_faction) const noexcept = 0;
        [[nodiscard]] virtual std::uint32_t GetTradeRouteCount(FactionId a_faction) const noexcept = 0;
        [[nodiscard]] virtual std::uint32_t GetGovernment(FactionId a_faction) const noexcept = 0;  // Id da doutrina

        // Eventos (callbacks na thread principal). a_user volta intacto no callback.
        virtual ListenerHandle AddListener(EventType a_type, EventCallback a_callback, void* a_user) noexcept = 0;
        virtual void           RemoveListener(ListenerHandle a_handle) noexcept = 0;

    protected:
        ~IAgeOfSkyrim() = default;  // a interface pertence ao Age of Skyrim: não delete
    };

#if __has_include("SKSE/SKSE.h")
}  // namespace AgeOfSkyrimAPI

#    include "SKSE/SKSE.h"

namespace AgeOfSkyrimAPI
{
    // Auxiliar para quem usa CommonLibSSE(-NG). Chame em kPostLoad ou depois.
    [[nodiscard]] inline IAgeOfSkyrim* RequestInterface(std::uint32_t a_version = kInterfaceVersion)
    {
        const auto messaging = SKSE::GetMessagingInterface();
        if (!messaging) {
            return nullptr;
        }
        InterfaceRequest req{ a_version, nullptr };
        messaging->Dispatch(kMessageRequestInterface, &req, static_cast<std::uint32_t>(sizeof(req)), kPluginName);
        return req.result;
    }
#endif
}
