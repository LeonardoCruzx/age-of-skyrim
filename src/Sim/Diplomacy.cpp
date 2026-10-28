#include "Sim/Diplomacy.h"

#include "Core/Factions.h"
#include "Core/I18n.h"
#include "Core/State.h"
#include "Core/Timer.h"
#include "Sim/Ai.h"
#include "Sim/Economy.h"
#include "Sim/Routes.h"
#include "World/Conquest.h"

namespace AoS
{
    namespace
    {
        const fs::path kDir       = "Data/SKSE/Plugins/AgeOfSkyrim/Factions";
        constexpr Id   kPlayerKey = MakeId("player");  // "player" nos JSON = qualquer facção do jogador

        [[nodiscard]] double HoursNow()
        {
            const auto cal = RE::Calendar::GetSingleton();
            return cal ? static_cast<double>(cal->GetHoursPassed()) : 0.0;
        }

        [[nodiscard]] std::optional<Diplomacy::Relation> ParseRelation(std::string_view a_text)
        {
            if (a_text == "peace") {
                return Diplomacy::Relation::kPeace;
            }
            if (a_text == "war") {
                return Diplomacy::Relation::kWar;
            }
            if (a_text == "alliance") {
                return Diplomacy::Relation::kAlliance;
            }
            if (a_text == "truce") {
                return Diplomacy::Relation::kTruce;
            }
            return std::nullopt;
        }

        [[nodiscard]] std::string_view LogName(Diplomacy::Relation a_relation)
        {
            switch (a_relation) {
            case Diplomacy::Relation::kWar:      return "guerra";
            case Diplomacy::Relation::kAlliance: return "aliança";
            case Diplomacy::Relation::kTruce:    return "trégua";
            default:                             return "paz";
            }
        }

        [[nodiscard]] bool IsPlayerFaction(Id a_faction)
        {
            const auto f = Factions::Get().Find(a_faction);
            return f && f->player;
        }
    }

    Diplomacy& Diplomacy::Get() noexcept
    {
        static Diplomacy instance;
        return instance;
    }

    std::uint64_t Diplomacy::PairKey(Id a_a, Id a_b) noexcept
    {
        const auto lo = std::min(a_a, a_b);
        const auto hi = std::max(a_a, a_b);
        return (static_cast<std::uint64_t>(lo) << 32) | hi;
    }

    std::string_view Diplomacy::Key(Relation a_relation) noexcept
    {
        switch (a_relation) {
        case Relation::kWar:      return "diplomacy.rel.war";
        case Relation::kAlliance: return "diplomacy.rel.alliance";
        case Relation::kTruce:    return "diplomacy.rel.truce";
        default:                  return "diplomacy.rel.peace";
        }
    }

    std::string_view Diplomacy::Key(Result a_result) noexcept
    {
        switch (a_result) {
        case Result::kOk:            return "diplomacy.result.ok";
        case Result::kAlreadyAtWar:  return "diplomacy.result.alreadyAtWar";
        case Result::kNotAtWar:      return "diplomacy.result.notAtWar";
        case Result::kInTruce:       return "diplomacy.result.inTruce";
        case Result::kNotAtPeace:    return "diplomacy.result.notAtPeace";
        case Result::kAlreadyAllied: return "diplomacy.result.alreadyAllied";
        case Result::kRefused:       return "diplomacy.result.refused";
        default:                     return "diplomacy.result.invalid";
        }
    }

    // ---- Dados ------------------------------------------------------------------------------------

    void Diplomacy::Load()
    {
        ScopedTimer timer{ "Diplomacy::Load" };
        {
            std::scoped_lock l{ lock_ };
            defaults_.clear();
            defaultRelation_ = Relation::kPeace;
            playerRelation_  = Relation::kPeace;
        }
        std::error_code       ec;
        std::vector<fs::path> files;
        for (const auto& entry : fs::directory_iterator(kDir, ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                files.push_back(entry.path());
            }
        }
        std::sort(files.begin(), files.end());
        for (const auto& f : files) {
            try {
                LoadFile(f);
            } catch (const std::exception& e) {
                logger::error("Diplomacia: erro em {}: {}", f.filename().string(), e.what());
            }
        }
        {
            std::scoped_lock l{ lock_ };
            logger::info("Diplomacia: {} relação(ões) padrão; entre facções {}, com o jogador {}.", defaults_.size(),
                         LogName(defaultRelation_), LogName(playerRelation_));
        }

        static bool registered = false;
        if (!registered) {
            registered = true;
            // Tomar território de uma facção de IA que não está em guerra com você = guerra (ADR 0049).
            Conquest::Get().AddListener([](const Territory& a_t, Id a_previous, Conquest::Source) {
                if (!Config::Get().Diplomacy().warOnConquest || a_previous == kNeutral ||
                    !Ai::Get().IsAiFaction(a_previous)) {
                    return;
                }
                const auto mine = Factions::Get().PlayerFaction();
                if (!mine || State::Get().Owner(a_t.id) != mine->id) {
                    return;
                }
                auto& d = Diplomacy::Get();
                if (d.RelationOf(mine->id, a_previous) != Relation::kWar) {
                    d.SetRelation(mine->id, a_previous, Relation::kWar);
                    logger::info("Diplomacia: '{}' declarou guerra ao jogador (perdeu '{}').",
                                 Factions::Get().NameOf(a_previous), a_t.key);
                    I18n::NotifyTr("diplomacy.warDeclaredOnYou", Factions::Get().NameOf(a_previous));
                }
            });
            Economy::Get().AddTickListener("diplomacy", [](std::uint32_t a_hours) { Diplomacy::Get().OnHours(a_hours); });
        }
    }

    // Formato (Factions/*.json, junto de "factions"):
    //   "defaultRelation": "peace", "playerRelation": "peace",
    //   "relations": [ { "a": "imperial", "b": "stormcloak", "relation": "war" } ]
    void Diplomacy::LoadFile(const fs::path& a_file)
    {
        std::ifstream in(a_file);
        if (!in) {
            return;
        }
        nlohmann::json json;
        try {
            json = nlohmann::json::parse(in, nullptr, true, /*ignore_comments*/ true);
        } catch (const std::exception& e) {
            logger::error("JSON inválido em {}: {}", a_file.string(), e.what());
            return;
        }
        if (!json.is_object()) {
            return;
        }
        const auto       name = a_file.filename().string();
        std::scoped_lock l{ lock_ };
        if (const auto r = ParseRelation(json.value("defaultRelation", ""s))) {
            defaultRelation_ = *r == Relation::kTruce ? Relation::kPeace : *r;
        }
        if (const auto r = ParseRelation(json.value("playerRelation", ""s))) {
            playerRelation_ = *r == Relation::kTruce ? Relation::kPeace : *r;
        }
        for (const auto& jr : json.value("relations", nlohmann::json::array())) {
            if (!jr.is_object()) {
                continue;
            }
            const auto a   = jr.value("a", ""s);
            const auto b   = jr.value("b", ""s);
            const auto rel = ParseRelation(jr.value("relation", ""s));
            if (a.empty() || b.empty() || a == b || !rel || *rel == Relation::kTruce) {
                logger::warn("{}: relação inválida ({} x {}); use \"peace\", \"war\" ou \"alliance\".", name, a, b);
                continue;
            }
            const auto ia = MakeId(a);
            const auto ib = MakeId(b);
            if ((ia != kPlayerKey && !Factions::Get().Find(ia)) || (ib != kPlayerKey && !Factions::Get().Find(ib))) {
                logger::warn("{}: relação {} x {}: facção desconhecida; ignorada.", name, a, b);
                continue;
            }
            defaults_[PairKey(ia, ib)] = Entry{ ia, ib, *rel, 0.0 };  // arquivo posterior substitui
            logger::info("Diplomacia: padrão {} x {} = {}.", a, b, LogName(*rel));
        }
    }

    // ---- Consulta -------------------------------------------------------------------------------

    Diplomacy::Relation Diplomacy::RelationUnlocked(Id a_a, Id a_b, double a_now) const
    {
        if (a_a == a_b || a_a == kNeutral || a_b == kNeutral) {
            return Relation::kPeace;
        }
        const auto key = PairKey(a_a, a_b);
        if (const auto it = overrides_.find(key); it != overrides_.end()) {
            if (it->second.relation == Relation::kTruce && a_now >= it->second.truceUntil) {
                return Relation::kPeace;  // venceu (OnHours grava e avisa)
            }
            return it->second.relation;
        }
        if (const auto it = defaults_.find(key); it != defaults_.end()) {
            return it->second.relation;
        }
        // Facções do jogador: "player" no JSON e depois playerRelation. Pré-definidas nunca são do jogador.
        const bool pa = !Ai::Get().IsAiFaction(a_a);
        const bool pb = !Ai::Get().IsAiFaction(a_b);
        if (pa && pb) {
            return Relation::kPeace;
        }
        if (pa || pb) {
            if (const auto it = defaults_.find(PairKey(kPlayerKey, pa ? a_b : a_a)); it != defaults_.end()) {
                return it->second.relation;
            }
            return playerRelation_;
        }
        return defaultRelation_;
    }

    Diplomacy::Relation Diplomacy::RelationOf(Id a_a, Id a_b) const
    {
        const auto       now = HoursNow();
        std::scoped_lock l{ lock_ };
        return RelationUnlocked(a_a, a_b, now);
    }

    bool Diplomacy::AtWar(Id a_a, Id a_b) const { return RelationOf(a_a, a_b) == Relation::kWar; }

    bool Diplomacy::AreHostile(Id a_a, Id a_b) const { return AtWar(a_a, a_b); }

    bool Diplomacy::CanTrade(Id a_a, Id a_b) const
    {
        if (a_a == kNeutral || a_b == kNeutral) {
            return false;
        }
        if (a_a == a_b) {
            return true;
        }
        const auto r = RelationOf(a_a, a_b);
        return r == Relation::kPeace || r == Relation::kAlliance;
    }

    double Diplomacy::TruceHoursLeft(Id a_a, Id a_b) const
    {
        const auto       now = HoursNow();
        std::scoped_lock l{ lock_ };
        const auto       it = overrides_.find(PairKey(a_a, a_b));
        if (it == overrides_.end() || it->second.relation != Relation::kTruce) {
            return 0.0;
        }
        return std::max(0.0, it->second.truceUntil - now);
    }

    std::vector<Diplomacy::Entry> Diplomacy::Explicit() const
    {
        const auto           ids = Factions::Get().Ids();  // poucas facções: n² barato
        const auto           now = HoursNow();
        std::vector<Entry>   out;
        std::scoped_lock     l{ lock_ };
        for (std::size_t i = 0; i < ids.size(); ++i) {
            for (std::size_t j = i + 1; j < ids.size(); ++j) {
                const auto r = RelationUnlocked(ids[i], ids[j], now);
                if (r != Relation::kPeace) {
                    double until = 0.0;
                    if (const auto it = overrides_.find(PairKey(ids[i], ids[j])); it != overrides_.end()) {
                        until = it->second.truceUntil;
                    }
                    out.push_back({ ids[i], ids[j], r, until });
                }
            }
        }
        return out;
    }

    std::vector<Id> Diplomacy::EnemiesOf(Id a_faction) const
    {
        std::vector<Id> out;
        for (const auto id : Factions::Get().Ids()) {
            if (id != a_faction && AtWar(a_faction, id)) {
                out.push_back(id);
            }
        }
        return out;
    }

    double Diplomacy::Power(Id a_faction) const
    {
        if (a_faction == kNeutral) {
            return 0.0;
        }
        double      power = 0.0;
        const auto& state = State::Get();
        const auto& ai    = Ai::Get();
        Config::Get().ForEachTerritory([&](const Territory& a_t) {
            if (state.Owner(a_t.id) == a_faction) {
                power += ai.DefenseOf(a_t, a_faction);
            }
        });
        const auto gold = Economy::Get().Balance(a_faction, Routes::Get().ResourceKey());
        return power + std::max(0.0, gold) / Config::Get().Ai().goldPerStrength;
    }

    // ---- Ações ----------------------------------------------------------------------------------

    void Diplomacy::SetUnlocked(Id a_a, Id a_b, Relation a_relation, double a_truceUntil)
    {
        overrides_[PairKey(a_a, a_b)] = Entry{ std::min(a_a, a_b), std::max(a_a, a_b), a_relation, a_truceUntil };
    }

    void Diplomacy::SetRelation(Id a_a, Id a_b, Relation a_relation, std::uint32_t a_truceHours)
    {
        if (a_a == a_b || a_a == kNeutral || a_b == kNeutral) {
            return;
        }
        const auto hours = a_truceHours ? a_truceHours : Config::Get().Diplomacy().truceHours;
        const auto until = a_relation == Relation::kTruce ? HoursNow() + hours : 0.0;
        {
            std::scoped_lock l{ lock_ };
            SetUnlocked(a_a, a_b, a_relation, until);
        }
        logger::info("Diplomacia: {} x {} = {}{}.", Factions::Get().NameOf(a_a), Factions::Get().NameOf(a_b),
                     LogName(a_relation), a_relation == Relation::kTruce ? std::format(" ({} h)", hours) : ""s);
    }

    Diplomacy::Result Diplomacy::DeclareWar(Id a_from, Id a_to)
    {
        auto& factions = Factions::Get();
        if (a_from == a_to || !factions.Find(a_from) || !factions.Find(a_to)) {
            return Result::kInvalid;
        }
        const auto r = RelationOf(a_from, a_to);
        if (r == Relation::kWar) {
            return Result::kAlreadyAtWar;
        }
        if (r == Relation::kTruce) {
            return Result::kInTruce;
        }
        SetRelation(a_from, a_to, Relation::kWar);
        if (IsPlayerFaction(a_from)) {
            I18n::NotifyTr("diplomacy.warDeclared", factions.NameOf(a_from), factions.NameOf(a_to));
        } else if (IsPlayerFaction(a_to)) {
            I18n::NotifyTr("diplomacy.warDeclaredOnYou", factions.NameOf(a_from));
        }
        return Result::kOk;
    }

    Diplomacy::Result Diplomacy::ProposePeace(Id a_from, Id a_to)
    {
        ScopedTimer timer{ "Diplomacy::ProposePeace" };
        auto&       factions = Factions::Get();
        if (a_from == a_to || !factions.Find(a_from) || !factions.Find(a_to)) {
            return Result::kInvalid;
        }
        if (RelationOf(a_from, a_to) != Relation::kWar) {
            return Result::kNotAtWar;
        }
        // A IA aceita se não está muito mais forte, ou se está em déficit (sem pagar a manutenção).
        bool accept = true;
        if (Ai::Get().IsAiFaction(a_to)) {
            const auto mine   = Power(a_from);
            const auto theirs = Power(a_to);
            accept = theirs < mine * Config::Get().Diplomacy().peaceAcceptRatio || Economy::Get().InDeficit(a_to);
            logger::info("Diplomacia: paz {} -> {}: poder {:.1f} x {:.1f} -> {}.", factions.NameOf(a_from),
                         factions.NameOf(a_to), mine, theirs, accept ? "aceita" : "recusada");
        }
        const bool player = IsPlayerFaction(a_from) || IsPlayerFaction(a_to);
        if (!accept) {
            if (player) {
                I18n::NotifyTr("diplomacy.peaceRefused", factions.NameOf(a_to));
            }
            return Result::kRefused;
        }
        const auto hours = Config::Get().Diplomacy().truceHours;
        SetRelation(a_from, a_to, hours > 0 ? Relation::kTruce : Relation::kPeace, hours);
        if (player) {
            I18n::NotifyTr("diplomacy.peaceAccepted", factions.NameOf(IsPlayerFaction(a_from) ? a_to : a_from), hours);
        }
        return Result::kOk;
    }

    Diplomacy::Result Diplomacy::ProposeAlliance(Id a_from, Id a_to)
    {
        ScopedTimer timer{ "Diplomacy::ProposeAlliance" };
        auto&       factions = Factions::Get();
        if (a_from == a_to || !factions.Find(a_from) || !factions.Find(a_to)) {
            return Result::kInvalid;
        }
        const auto r = RelationOf(a_from, a_to);
        if (r == Relation::kAlliance) {
            return Result::kAlreadyAllied;
        }
        if (r != Relation::kPeace) {
            return r == Relation::kTruce ? Result::kInTruce : Result::kNotAtPeace;
        }
        bool accept = true;
        if (Ai::Get().IsAiFaction(a_to)) {
            const auto& cfg    = Config::Get().Diplomacy();
            const auto  mine   = Power(a_from);
            const auto  theirs = Power(a_to);
            bool        common = !cfg.allianceNeedsCommonEnemy;
            if (!common) {
                const auto enemies = EnemiesOf(a_to);
                common = std::any_of(enemies.begin(), enemies.end(), [&](Id e) { return AtWar(a_from, e); });
            }
            accept = common && mine >= theirs * cfg.allianceAcceptRatio;
            logger::info("Diplomacia: aliança {} -> {}: poder {:.1f} x {:.1f}, inimigo comum {} -> {}.",
                         factions.NameOf(a_from), factions.NameOf(a_to), mine, theirs, common,
                         accept ? "aceita" : "recusada");
        }
        const bool player = IsPlayerFaction(a_from) || IsPlayerFaction(a_to);
        if (!accept) {
            if (player) {
                I18n::NotifyTr("diplomacy.allianceRefused", factions.NameOf(a_to));
            }
            return Result::kRefused;
        }
        SetRelation(a_from, a_to, Relation::kAlliance);
        if (player) {
            I18n::NotifyTr("diplomacy.allianceAccepted", factions.NameOf(IsPlayerFaction(a_from) ? a_to : a_from));
        }
        return Result::kOk;
    }

    void Diplomacy::DebugCycleHere()
    {
        const auto mine = Factions::Get().PlayerFaction();
        if (!mine) {
            I18n::NotifyTr("diplomacy.noPlayerFaction");
            return;
        }
        const auto* t     = Conquest::Get().TerritoryAtPlayer(/*onlyCleared*/ false);
        const auto  owner = t ? State::Get().Owner(t->id) : kNeutral;
        if (!t || owner == kNeutral || owner == mine->id) {
            I18n::NotifyTr("diplomacy.debugNoOwner");
            return;
        }
        Relation next = Relation::kWar;
        switch (RelationOf(mine->id, owner)) {
        case Relation::kWar:      next = Relation::kTruce; break;
        case Relation::kTruce:    next = Relation::kAlliance; break;
        case Relation::kAlliance: next = Relation::kPeace; break;
        default:                  next = Relation::kWar; break;
        }
        SetRelation(mine->id, owner, next);
        I18n::NotifyTr("diplomacy.debugRelation", Factions::Get().NameOf(owner), I18n::Tr(Key(next)));
    }

    // ---- Tick -------------------------------------------------------------------------------------

    void Diplomacy::OnHours(std::uint32_t)
    {
        const auto      now = HoursNow();
        std::vector<Id> ended;  // pares do jogador cuja trégua acabou: avisa fora do lock
        {
            std::scoped_lock l{ lock_ };
            for (auto& [key, e] : overrides_) {
                if (e.relation == Relation::kTruce && now >= e.truceUntil) {
                    e.relation   = Relation::kPeace;
                    e.truceUntil = 0.0;
                    logger::info("Diplomacia: trégua {} x {} terminou; agora paz.", Factions::Get().NameOf(e.a),
                                 Factions::Get().NameOf(e.b));
                    if (!Ai::Get().IsAiFaction(e.a)) {
                        ended.push_back(e.b);
                    } else if (!Ai::Get().IsAiFaction(e.b)) {
                        ended.push_back(e.a);
                    }
                }
            }
        }
        for (const auto other : ended) {
            I18n::NotifyTr("diplomacy.truceEnded", Factions::Get().NameOf(other));
        }
    }

    // ---- Cosave -----------------------------------------------------------------------------------

    void Diplomacy::Clear()
    {
        std::scoped_lock l{ lock_ };
        overrides_.clear();
    }

    // 'DIPL' v1: u32 count, count × { u32 a, u32 b, u8 relação, f64 fim da trégua (horas de jogo) }.
    void Diplomacy::Save(SKSE::SerializationInterface* a_intfc) const
    {
        std::scoped_lock l{ lock_ };
        if (overrides_.empty()) {
            return;
        }
        if (!a_intfc->OpenRecord(kRecord, kRecordVersion)) {
            logger::error("Cosave: falha ao abrir o registro DIPL.");
            return;
        }
        a_intfc->WriteRecordData(static_cast<std::uint32_t>(overrides_.size()));
        for (const auto& [key, e] : overrides_) {
            a_intfc->WriteRecordData(e.a);
            a_intfc->WriteRecordData(e.b);
            a_intfc->WriteRecordData(static_cast<std::uint8_t>(e.relation));
            a_intfc->WriteRecordData(e.truceUntil);
        }
    }

    void Diplomacy::LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version)
    {
        if (a_version != kRecordVersion) {
            logger::warn("Cosave: versão {} do registro DIPL não suportada; relações voltam ao padrão.", a_version);
            return;
        }
        std::uint32_t count{};
        a_intfc->ReadRecordData(count);
        std::size_t      dropped = 0;
        std::scoped_lock l{ lock_ };
        for (std::uint32_t i = 0; i < count; ++i) {
            Id            a{}, b{};
            std::uint8_t  rel{};
            double        until{};
            a_intfc->ReadRecordData(a);
            a_intfc->ReadRecordData(b);
            a_intfc->ReadRecordData(rel);
            a_intfc->ReadRecordData(until);
            // FACT é lido antes (ordem de escrita): facção que não existe mais é descartada.
            if (!Factions::Get().Find(a) || !Factions::Get().Find(b) || rel > static_cast<std::uint8_t>(Relation::kTruce)) {
                ++dropped;
                continue;
            }
            SetUnlocked(a, b, static_cast<Relation>(rel), until);
        }
        logger::info("Cosave: {} relação(ões) diplomática(s) carregada(s), {} descartada(s).", overrides_.size(),
                     dropped);
    }
}
