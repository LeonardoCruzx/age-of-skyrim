#pragma once

#include "Realm/Data.h"

#include <functional>

namespace AoS::Realm
{
    // Governo da facção (M18): doutrina escolhida, cargos (mordomo/comandante/ferreiro...) e governadores
    // por província. Registro de cosave 'GOVT' v1 (inclui o relógio do reino).
    class Government
    {
    public:
        [[nodiscard]] static Government& Get() noexcept;

        static constexpr std::uint32_t kRecord        = Tag("GOVT");
        static constexpr std::uint32_t kRecordVersion = 1;

        enum class Result : std::uint8_t
        {
            kOk,
            kUnknown,     // facção, doutrina, cargo ou província desconhecidos
            kSame,        // já é a doutrina atual
            kLocked,      // título insuficiente
            kCooldown,    // espera entre trocas de doutrina
            kNoPayment,   // gancho de pagamento recusou (estoque insuficiente)
            kNoSlot,      // título não dá mais vagas de cargo/governador
            kNoMajority,  // governador: facção sem maioria na província
        };

        // Gancho da economia (M12): debita o custo do estoque da sede; false = não pagou. Sem gancho, troca grátis.
        // Chamado FORA do mutex do reino.
        using PaymentHandler = std::function<bool(Id a_faction, const std::vector<Cost>& a_cost)>;
        void SetPaymentHandler(PaymentHandler a_handler);

        // ---- Doutrina ----
        [[nodiscard]] Id     DoctrineOf(Id a_faction) const;  // escolhida, ou Data::DefaultDoctrine()
        [[nodiscard]] bool   HasChosenDoctrine(Id a_faction) const;
        [[nodiscard]] float  CooldownLeft(Id a_faction) const;  // horas até poder trocar (0 = pode)
        // 1ª escolha (fundação) é livre: sem custo nem espera. Depois: custo da doutrina nova + doctrineCooldownHours.
        Result SetDoctrine(Id a_faction, Id a_doctrine);

        // ---- Cargos ----
        // O cargo vale (bônus) assim que atribuído. a_npc = base do NPC escolhido (0 = vaga preenchida sem NPC).
        // Colocar o NPC na sede (base vanilla + outfit/pacote nossos) fica para depois da sede (M6) — ADR 0036.
        Result AssignOfficer(Id a_faction, Id a_office, RE::FormID a_npc = 0);
        bool   RemoveOfficer(Id a_faction, Id a_office);
        [[nodiscard]] std::size_t OfficerCount(Id a_faction) const;
        [[nodiscard]] bool        HasOfficer(Id a_faction, Id a_office) const;

        // ---- Governadores ----
        Result AssignGovernor(Id a_faction, Id a_province, RE::FormID a_npc = 0);
        bool   RemoveGovernor(Id a_faction, Id a_province);
        [[nodiscard]] std::size_t GovernorCount(Id a_faction) const;
        [[nodiscard]] bool        HasGovernor(Id a_faction, Id a_province) const;
        // Remove governadores de províncias fora de a_majority; devolve as removidas.
        std::vector<Id> ValidateGovernors(Id a_faction, const std::vector<Id>& a_majority);

        // Soma doutrina + cargos da facção em a_io (Modifiers).
        void AddModifiers(Id a_faction, ModifierSet& a_io) const;

        // ---- Relógio do reino ----
        [[nodiscard]] double Clock() const;
        void                 Advance(float a_hours);

        void Clear();
        void Save(SKSE::SerializationInterface* a_intfc) const;
        void LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version);

    private:
        struct Post
        {
            Id         id{ kNeutral };  // cargo ou província
            RE::FormID npc{ 0 };
        };

        struct FactionGov
        {
            Id                doctrine{ kNeutral };  // kNeutral = ainda não escolheu (usa a padrão)
            double            lastChange{ -1.0 };    // relógio da última troca (-1 = nunca)
            std::vector<Post> officers;
            std::vector<Post> governors;
        };

        [[nodiscard]] const FactionGov* FindUnlocked(Id a_faction) const;

        std::unordered_map<Id, FactionGov> factions_;
        double                             clock_{ 0.0 };
        PaymentHandler                     pay_;
    };

    [[nodiscard]] std::string_view ToString(Government::Result a_result) noexcept;  // para log
}
