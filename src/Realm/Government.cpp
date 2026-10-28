#include "Realm/Government.h"

#include "Api/Events.h"
#include "Core/Factions.h"
#include "Realm/Realm.h"
#include "Realm/Titles.h"

#include <limits>

namespace AoS::Realm
{
    namespace
    {
        // Vagas pelo título atual ("officers"/"governors" em Titles/*.json); sem título declarado = sem limite.
        std::size_t Slots(Id a_faction, std::string_view a_key)
        {
            const auto v = Titles::Get().Unlock(a_faction, a_key, -1.0f);
            return v < 0.0f ? std::numeric_limits<std::size_t>::max() : static_cast<std::size_t>(v);
        }

        template <class List>
        auto FindPost(List& a_list, Id a_id)
        {
            return std::find_if(a_list.begin(), a_list.end(), [&](const auto& p) { return p.id == a_id; });
        }
    }

    std::string_view ToString(Government::Result a_result) noexcept
    {
        switch (a_result) {
        case Government::Result::kOk:         return "ok";
        case Government::Result::kUnknown:    return "desconhecido";
        case Government::Result::kSame:       return "já é a atual";
        case Government::Result::kLocked:     return "título insuficiente";
        case Government::Result::kCooldown:   return "em espera";
        case Government::Result::kNoPayment:  return "custo não pago";
        case Government::Result::kNoSlot:     return "sem vaga";
        case Government::Result::kNoMajority: return "sem maioria na província";
        default:                              return "?";
        }
    }

    Government& Government::Get() noexcept
    {
        static Government instance;
        return instance;
    }

    void Government::SetPaymentHandler(PaymentHandler a_handler)
    {
        std::scoped_lock l{ Mutex() };
        pay_ = std::move(a_handler);
        logger::info("Governo: gancho de pagamento {}.", pay_ ? "registrado" : "removido");
    }

    const Government::FactionGov* Government::FindUnlocked(Id a_faction) const
    {
        const auto it = factions_.find(a_faction);
        return it == factions_.end() ? nullptr : &it->second;
    }

    Id Government::DoctrineOf(Id a_faction) const
    {
        std::scoped_lock l{ Mutex() };
        const auto       g = FindUnlocked(a_faction);
        return g && g->doctrine != kNeutral ? g->doctrine : Data::Get().DefaultDoctrine();
    }

    bool Government::HasChosenDoctrine(Id a_faction) const
    {
        std::scoped_lock l{ Mutex() };
        const auto       g = FindUnlocked(a_faction);
        return g && g->doctrine != kNeutral;
    }

    float Government::CooldownLeft(Id a_faction) const
    {
        std::scoped_lock l{ Mutex() };
        const auto       g = FindUnlocked(a_faction);
        if (!g || g->lastChange < 0.0) {
            return 0.0f;
        }
        const auto left = g->lastChange + Config::Get().Realm().doctrineCooldownHours - clock_;
        return left > 0.0 ? static_cast<float>(left) : 0.0f;
    }

    Government::Result Government::SetDoctrine(Id a_faction, Id a_doctrine)
    {
        const auto doctrine = Data::Get().FindDoctrine(a_doctrine);
        if (!doctrine || !Factions::Get().Find(a_faction)) {
            return Result::kUnknown;
        }

        PaymentHandler    pay;
        std::vector<Cost> cost;
        {
            std::scoped_lock l{ Mutex() };
            const auto       g      = FindUnlocked(a_faction);
            const bool       chosen = g && g->doctrine != kNeutral;
            if (chosen && g->doctrine == a_doctrine) {
                return Result::kSame;
            }
            if (!Titles::Get().Meets(a_faction, doctrine->requiresTitle)) {
                return Result::kLocked;
            }
            if (chosen) {  // troca (não a fundação): espera + custo
                if (CooldownLeft(a_faction) > 0.0f) {
                    return Result::kCooldown;
                }
                cost = doctrine->changeCost;
                pay  = pay_;
            }
        }

        // Pagamento fora do mutex: o gancho da economia pode travar o próprio estado.
        if (!cost.empty()) {
            if (pay) {
                if (!pay(a_faction, cost)) {
                    return Result::kNoPayment;
                }
            } else {
                logger::info("Governo: sem economia registrada; troca de doutrina sem custo.");
            }
        }

        std::scoped_lock l{ Mutex() };
        auto&            g     = factions_[a_faction];
        const bool       first = g.doctrine == kNeutral;
        g.doctrine             = a_doctrine;
        g.lastChange           = first ? -1.0 : clock_;  // a fundação não inicia espera
        logger::info("Governo: '{}' adota a doutrina '{}' ({}).", Factions::Get().NameOf(a_faction), doctrine->key,
                     first ? "fundação" : "troca");
        Api::Events::GovernmentChanged(a_faction);
        return Result::kOk;
    }

    Government::Result Government::AssignOfficer(Id a_faction, Id a_office, RE::FormID a_npc)
    {
        const auto office = Data::Get().FindOffice(a_office);
        if (!office || !Factions::Get().Find(a_faction)) {
            return Result::kUnknown;
        }
        std::scoped_lock l{ Mutex() };
        if (!Titles::Get().Meets(a_faction, office->requiresTitle)) {
            return Result::kLocked;
        }
        auto&      g  = factions_[a_faction];
        const auto it = FindPost(g.officers, a_office);
        if (it != g.officers.end()) {
            it->npc = a_npc;  // troca o ocupante, mesma vaga
        } else {
            if (g.officers.size() >= Slots(a_faction, "officers")) {
                return Result::kNoSlot;
            }
            g.officers.push_back({ a_office, a_npc });
        }
        logger::info("Governo: '{}' nomeia {} (NPC 0x{:08X}).", Factions::Get().NameOf(a_faction), office->key, a_npc);
        return Result::kOk;
    }

    bool Government::RemoveOfficer(Id a_faction, Id a_office)
    {
        std::scoped_lock l{ Mutex() };
        const auto       f = factions_.find(a_faction);
        if (f == factions_.end()) {
            return false;
        }
        const auto it = FindPost(f->second.officers, a_office);
        if (it == f->second.officers.end()) {
            return false;
        }
        f->second.officers.erase(it);
        return true;
    }

    std::size_t Government::OfficerCount(Id a_faction) const
    {
        std::scoped_lock l{ Mutex() };
        const auto       g = FindUnlocked(a_faction);
        return g ? g->officers.size() : 0;
    }

    bool Government::HasOfficer(Id a_faction, Id a_office) const
    {
        std::scoped_lock l{ Mutex() };
        const auto       g = FindUnlocked(a_faction);
        return g && std::ranges::any_of(g->officers, [&](const Post& p) { return p.id == a_office; });
    }

    Government::Result Government::AssignGovernor(Id a_faction, Id a_province, RE::FormID a_npc)
    {
        if (Data::Get().ProvinceSize(a_province) == 0 || !Factions::Get().Find(a_faction)) {
            return Result::kUnknown;
        }
        const auto stats = ComputeStats(a_faction);  // O(territórios), só em ação do jogador
        if (std::ranges::find(stats.majorityProvinces, a_province) == stats.majorityProvinces.end()) {
            return Result::kNoMajority;
        }
        std::scoped_lock l{ Mutex() };
        auto&            g  = factions_[a_faction];
        const auto       it = FindPost(g.governors, a_province);
        if (it != g.governors.end()) {
            it->npc = a_npc;
        } else {
            if (g.governors.size() >= Slots(a_faction, "governors")) {
                return Result::kNoSlot;
            }
            g.governors.push_back({ a_province, a_npc });
        }
        logger::info("Governo: '{}' nomeia governador para {} (NPC 0x{:08X}).", Factions::Get().NameOf(a_faction),
                     Config::Get().NameOf(a_province), a_npc);
        return Result::kOk;
    }

    bool Government::RemoveGovernor(Id a_faction, Id a_province)
    {
        std::scoped_lock l{ Mutex() };
        const auto       f = factions_.find(a_faction);
        if (f == factions_.end()) {
            return false;
        }
        const auto it = FindPost(f->second.governors, a_province);
        if (it == f->second.governors.end()) {
            return false;
        }
        f->second.governors.erase(it);
        return true;
    }

    std::size_t Government::GovernorCount(Id a_faction) const
    {
        std::scoped_lock l{ Mutex() };
        const auto       g = FindUnlocked(a_faction);
        return g ? g->governors.size() : 0;
    }

    bool Government::HasGovernor(Id a_faction, Id a_province) const
    {
        if (a_province == kNeutral) {
            return false;
        }
        std::scoped_lock l{ Mutex() };
        const auto       g = FindUnlocked(a_faction);
        return g && std::ranges::any_of(g->governors, [&](const Post& p) { return p.id == a_province; });
    }

    std::vector<Id> Government::ValidateGovernors(Id a_faction, const std::vector<Id>& a_majority)
    {
        std::vector<Id>  removed;
        std::scoped_lock l{ Mutex() };
        const auto       f = factions_.find(a_faction);
        if (f == factions_.end()) {
            return removed;
        }
        std::erase_if(f->second.governors, [&](const Post& p) {
            if (std::ranges::find(a_majority, p.id) != a_majority.end()) {
                return false;
            }
            removed.push_back(p.id);
            return true;
        });
        for (const auto p : removed) {
            logger::info("Governo: '{}' perdeu a maioria em {}; governador removido.", Factions::Get().NameOf(a_faction),
                         Config::Get().NameOf(p));
        }
        return removed;
    }

    void Government::AddModifiers(Id a_faction, ModifierSet& a_io) const
    {
        const auto&      data = Data::Get();
        std::scoped_lock l{ Mutex() };
        const auto       g = FindUnlocked(a_faction);
        if (const auto d = data.FindDoctrine(g && g->doctrine != kNeutral ? g->doctrine : data.DefaultDoctrine())) {
            for (std::size_t i = 0; i < kModifierCount; ++i) {
                a_io[i] += d->modifiers[i];
            }
        }
        if (!g) {
            return;
        }
        for (const auto& p : g->officers) {
            if (const auto o = data.FindOffice(p.id)) {
                for (std::size_t i = 0; i < kModifierCount; ++i) {
                    a_io[i] += o->modifiers[i];
                }
            }
        }
    }

    double Government::Clock() const
    {
        std::scoped_lock l{ Mutex() };
        return clock_;
    }

    void Government::Advance(float a_hours)
    {
        std::scoped_lock l{ Mutex() };
        clock_ += std::max(0.0f, a_hours);
    }

    void Government::Clear()
    {
        std::scoped_lock l{ Mutex() };
        factions_.clear();
        clock_ = 0.0;
    }

    // Formato do registro 'GOVT' v1:
    //   f64 clock, u32 count, depois count × { u32 faction, u32 doctrine, f64 lastChange,
    //     u8 nOfficers × { u32 office, u32 npcFormID }, u8 nGovernors × { u32 province, u32 npcFormID } }.
    void Government::Save(SKSE::SerializationInterface* a_intfc) const
    {
        std::scoped_lock l{ Mutex() };
        if (!a_intfc->OpenRecord(kRecord, kRecordVersion)) {
            logger::error("Cosave: falha ao abrir o registro GOVT.");
            return;
        }
        std::size_t bytes = sizeof(double) + sizeof(std::uint32_t);
        a_intfc->WriteRecordData(clock_);
        a_intfc->WriteRecordData(static_cast<std::uint32_t>(factions_.size()));
        const auto writePosts = [&](const std::vector<Post>& a_list) {
            const auto n = static_cast<std::uint8_t>(std::min<std::size_t>(a_list.size(), 0xFF));
            a_intfc->WriteRecordData(n);
            for (std::size_t i = 0; i < n; ++i) {
                a_intfc->WriteRecordData(a_list[i].id);
                a_intfc->WriteRecordData(a_list[i].npc);
            }
            bytes += 1 + n * 8u;
        };
        for (const auto& [faction, g] : factions_) {
            a_intfc->WriteRecordData(faction);
            a_intfc->WriteRecordData(g.doctrine);
            a_intfc->WriteRecordData(g.lastChange);
            bytes += 8 + sizeof(double);
            writePosts(g.officers);
            writePosts(g.governors);
        }
        logger::info("Cosave: GOVT v{} salvo ({} facção(ões), relógio {:.1f} h, {} bytes).", kRecordVersion,
                     factions_.size(), clock_, bytes);
    }

    void Government::LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version)
    {
        if (a_version != kRecordVersion) {
            logger::warn("Cosave: versão {} do registro GOVT não suportada; governo volta ao padrão.", a_version);
            return;
        }
        const auto&      data = Data::Get();
        std::scoped_lock l{ Mutex() };
        std::uint32_t    count{};
        a_intfc->ReadRecordData(clock_);
        a_intfc->ReadRecordData(count);

        const auto readPosts = [&](std::vector<Post>& a_out, auto&& a_valid, std::string_view a_what) {
            std::uint8_t n{};
            a_intfc->ReadRecordData(n);
            for (std::uint8_t i = 0; i < n; ++i) {
                Post p;
                a_intfc->ReadRecordData(p.id);
                a_intfc->ReadRecordData(p.npc);
                if (p.npc != 0 && !a_intfc->ResolveFormID(p.npc, p.npc)) {
                    p.npc = 0;  // plugin do NPC saiu da load order: a vaga continua, sem NPC
                }
                if (a_valid(p.id)) {
                    a_out.push_back(p);
                } else {
                    logger::warn("Cosave: {} 0x{:08X} não existe mais; descartado.", a_what, p.id);
                }
            }
        };

        for (std::uint32_t i = 0; i < count; ++i) {
            Id         faction{};
            FactionGov g;
            a_intfc->ReadRecordData(faction);
            a_intfc->ReadRecordData(g.doctrine);
            a_intfc->ReadRecordData(g.lastChange);
            readPosts(g.officers, [&](Id a_id) { return data.FindOffice(a_id) != nullptr; }, "cargo");
            readPosts(g.governors, [&](Id a_id) { return data.ProvinceSize(a_id) > 0; }, "província");

            if (!Factions::Get().Find(faction)) {
                logger::warn("Cosave: GOVT de facção 0x{:08X} que não existe mais; descartado.", faction);
                continue;
            }
            if (g.doctrine != kNeutral && !data.FindDoctrine(g.doctrine)) {
                logger::warn("Cosave: doutrina 0x{:08X} não existe mais; facção volta à padrão.", g.doctrine);
                g.doctrine = kNeutral;
            }
            factions_[faction] = std::move(g);
        }
        logger::info("Cosave: GOVT v{} carregado ({} facção(ões), relógio {:.1f} h).", a_version, factions_.size(), clock_);
    }
}
