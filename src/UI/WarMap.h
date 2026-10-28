#pragma once

#include "Core/Config.h"

#include <atomic>
#include <functional>

namespace AoS
{
    // M11: mesa de guerra da sede -> mapa tático (ADR 0013 / 0051-0054).
    //  - Com PrismaUI (dependência opcional, detectada em runtime): view HTML/JS própria em
    //    Data/PrismaUI/views/AgeOfSkyrim/ que desenha o mapa em SVG a partir das posições dos territórios.
    //  - Sem PrismaUI: menus vanilla (MessageBox) com as ações essenciais (UI/WarMapMenus).
    //  - Dados vão para a view como UM snapshot JSON (schema em docs/warmap-schema.md), só ao abrir e quando algo
    //    muda (ações do mapa, conquista, NotifyChanged de outro módulo). Nada de polling.
    //  - Camadas extras por provedor registrável (guarnição M14, ameaça M16, renda...): o mapa não conhece os módulos.
    //  - Alvos de ataque marcados ficam no cosave 'WMAP' v1 (para a IA/ataques da M16 lerem via Targets()).
    // Ações e montagem do snapshot só na thread principal; consultas de alvos são thread-safe.
    class WarMap
    {
    public:
        // Camada de dados opcional: devolve o valor da camada para o território (objeto, número ou texto), ou
        // null/vazio se não tem dado para ele. Chamado na thread principal ao montar o snapshot (uma vez por
        // território por envio). Não chamar o WarMap de dentro. Ex.:
        //   WarMap::Get().AddProvider("garrison", "warmap.layer.garrison",
        //       [](const Territory& t) { return nlohmann::json{ { "soldiers", Garrison::Count(t.id) } }; });
        using Provider = std::function<nlohmann::json(const Territory& a_territory)>;

        struct ActionResult
        {
            bool        ok{ false };
            std::string text;  // já traduzido
        };

        [[nodiscard]] static WarMap& Get() noexcept;

        static constexpr std::uint32_t kRecord        = Tag("WMAP");
        static constexpr std::uint32_t kRecordVersion = 1;

        // kDataLoaded, depois de Config/Seat: detecta o PrismaUI (ou decide pelos menus vanilla) e liga o refresh
        // na conquista.
        void Load();

        // Thread principal: abre o mapa (PrismaUI ou menus vanilla). Chamado por Seat::OnWarTableActivated.
        void Open();
        void Close();
        [[nodiscard]] bool IsOpen() const;
        [[nodiscard]] bool UsingPrismaUI() const noexcept { return prisma_.load(std::memory_order_relaxed); }

        // ---- camadas (M14/M16/M9...) ----
        // a_labelKey: chave de tradução do nome da camada (painel do território). Mesma chave substitui.
        void AddProvider(std::string_view a_key, std::string_view a_labelKey, Provider a_provider);
        void RemoveProvider(std::string_view a_key);

        // Qualquer thread: algo que o mapa mostra mudou. Se a view está aberta, reenvia o snapshot (coalescido
        // numa task; várias chamadas seguidas = um envio).
        void NotifyChanged();

        // ---- alvos de ataque (M16 lê) ----
        [[nodiscard]] bool            IsTarget(Id a_territory) const;
        [[nodiscard]] std::vector<Id> Targets() const;

        // ---- ações (thread principal): usadas pela view e pelos menus vanilla ----
        ActionResult CreateRoute(Id a_territoryA, Id a_territoryB);
        ActionResult RemoveRoute(Id a_route);
        ActionResult SetTarget(Id a_territory, bool a_on);

        // Snapshot completo (thread principal). Ver docs/warmap-schema.md.
        [[nodiscard]] std::string BuildSnapshot() const;

        // Recebe a mensagem da view (JSON {"action": ...}) de qualquer thread; executa numa task.
        void HandleViewMessage(std::string a_message);

        void Clear();
        void Save(SKSE::SerializationInterface* a_intfc) const;
        void LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version);

        // Nome da camada (já traduzido) e valor, para o painel dos menus vanilla.
        [[nodiscard]] std::vector<std::pair<std::string, nlohmann::json>> LayersOf(const Territory& a_territory) const;

    private:
        struct Layer
        {
            std::string key;
            std::string labelKey;
            Provider    provider;
        };

        void PushSnapshot();  // thread principal, só se a view está aberta

        mutable std::mutex     lock_;
        std::vector<Id>        targets_;  // ordem de marcação (poucos: maxTargets)
        std::vector<Layer>     layers_;   // thread principal (AddProvider em kDataLoaded/kPostLoad)
        std::atomic<bool>      prisma_{ false };
        std::atomic<bool>      refreshQueued_{ false };
        bool                   loaded_{ false };
    };
}
