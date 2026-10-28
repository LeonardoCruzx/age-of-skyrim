#include "Core/Factions.h"

#include "Api/Events.h"
#include "Core/Config.h"
#include "Core/I18n.h"

namespace AoS
{
    namespace
    {
        const fs::path kDir = "Data/SKSE/Plugins/AgeOfSkyrim/Factions";

        std::optional<std::uint32_t> ParseColor(const nlohmann::json& a_value)
        {
            if (!a_value.is_string()) {
                return std::nullopt;
            }
            auto text = std::string_view{ a_value.get_ref<const std::string&>() };
            if (text.starts_with('#')) {
                text.remove_prefix(1);
            }
            std::uint32_t value{};
            const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
            if (ec != std::errc{} || ptr != text.data() + text.size() || text.size() != 6) {
                return std::nullopt;
            }
            return value;
        }
    }

    Factions& Factions::Get() noexcept
    {
        static Factions instance;
        return instance;
    }

    void Factions::Load()
    {
        {
            std::scoped_lock l{ lock_ };
            predefined_.clear();
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
            LoadFile(f);
        }
        logger::info("Facções: {} pré-definida(s).", predefined_.size());
    }

    // Formato: { "factions": [ { "id": "imperial", "name": {"en": .., "pt-br": ..}, "style": "imperial",
    //                             "color": "#9B1C1C" } ] }
    void Factions::LoadFile(const fs::path& a_file)
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
        const auto list = json.find("factions");
        if (list == json.end() || !list->is_array()) {
            return;
        }

        const auto&      styles = Config::Get().Styles();
        std::scoped_lock l{ lock_ };
        for (const auto& jf : *list) {
            Faction f;
            f.key = jf.value("id", std::string{});
            if (f.key.empty()) {
                logger::warn("{}: facção sem 'id', ignorada.", a_file.filename().string());
                continue;
            }
            f.id = MakeId(f.key);
            if (FindUnlocked(f.id)) {
                logger::warn("Facção duplicada '{}', ignorada.", f.key);
                continue;
            }
            f.name  = jf.contains("name") ? I18n::Pick(jf["name"], f.key) : f.key;
            f.style = MakeId(jf.value("style", std::string{}));
            if (std::find(styles.begin(), styles.end(), f.style) == styles.end()) {
                logger::warn("Facção '{}': estilo '{}' não declarado; ignorada.", f.key, jf.value("style", std::string{}));
                continue;
            }
            if (const auto c = jf.find("color"); c != jf.end()) {
                if (const auto parsed = ParseColor(*c)) {
                    f.color = *parsed;
                } else {
                    logger::warn("Facção '{}': cor inválida (use \"#RRGGBB\").", f.key);
                }
            }
            predefined_.push_back(std::move(f));
        }
    }

    const Faction* Factions::FindUnlocked(Id a_id) const
    {
        for (const auto* list : { &predefined_, &player_ }) {
            for (const auto& f : *list) {
                if (f.id == a_id) {
                    return &f;
                }
            }
        }
        return nullptr;
    }

    Faction* Factions::FindPlayerUnlocked(Id a_id)
    {
        for (auto& f : player_) {
            if (f.id == a_id) {
                return &f;
            }
        }
        return nullptr;
    }

    std::optional<Faction> Factions::Find(Id a_id) const
    {
        std::scoped_lock l{ lock_ };
        const auto       f = FindUnlocked(a_id);
        return f ? std::optional{ *f } : std::nullopt;
    }

    std::vector<Faction> Factions::All() const
    {
        std::scoped_lock     l{ lock_ };
        std::vector<Faction> out(predefined_);
        out.insert(out.end(), player_.begin(), player_.end());
        return out;
    }

    std::vector<Id> Factions::Ids() const
    {
        std::scoped_lock l{ lock_ };
        std::vector<Id>  out;
        out.reserve(predefined_.size() + player_.size());
        for (const auto* list : { &predefined_, &player_ }) {
            for (const auto& f : *list) {
                out.push_back(f.id);
            }
        }
        return out;
    }

    bool Factions::IsValidOwner(Id a_owner) const
    {
        std::scoped_lock l{ lock_ };
        return a_owner == kNeutral || FindUnlocked(a_owner) != nullptr;
    }

    Id Factions::StyleOf(Id a_owner) const
    {
        std::scoped_lock l{ lock_ };
        const auto       f = FindUnlocked(a_owner);
        return f ? f->style : kNeutral;
    }

    std::string Factions::NameOf(Id a_owner) const
    {
        if (a_owner == kNeutral) {
            return "neutral";
        }
        std::scoped_lock l{ lock_ };
        const auto       f = FindUnlocked(a_owner);
        return f ? f->name : std::format("0x{:08X}", a_owner);
    }

    std::optional<Faction> Factions::PlayerFaction() const
    {
        std::scoped_lock l{ lock_ };
        return player_.empty() ? std::nullopt : std::optional{ player_.front() };
    }

    std::optional<Id> Factions::CreatePlayerFaction(std::string a_name, Id a_style, std::uint32_t a_color)
    {
        const auto& styles = Config::Get().Styles();
        if (std::find(styles.begin(), styles.end(), a_style) == styles.end()) {
            logger::warn("CreatePlayerFaction: estilo 0x{:08X} desconhecido.", a_style);
            return std::nullopt;
        }
        std::scoped_lock l{ lock_ };
        Faction          f;
        // Chave estável dentro do save; números nunca são reaproveitados.
        do {
            f.key = std::format("player_{}", nextPlayer_++);
            f.id  = MakeId(f.key);
        } while (FindUnlocked(f.id));
        f.name   = std::move(a_name);
        f.style  = a_style;
        f.color  = a_color & 0xFFFFFF;
        f.player = true;
        logger::info("Facção do jogador criada: '{}' ({}).", f.name, f.key);
        player_.push_back(std::move(f));
        Api::Events::FactionCreated(player_.back().id);  // só enfileira: seguro sob o lock
        return player_.back().id;
    }

    bool Factions::Rename(Id a_id, std::string a_name)
    {
        std::scoped_lock l{ lock_ };
        const auto       f = FindPlayerUnlocked(a_id);
        if (!f || a_name.empty()) {
            return false;
        }
        f->name = std::move(a_name);
        Api::Events::FactionChanged(a_id);
        return true;
    }

    bool Factions::SetStyle(Id a_id, Id a_style)
    {
        const auto& styles = Config::Get().Styles();
        if (std::find(styles.begin(), styles.end(), a_style) == styles.end()) {
            return false;
        }
        std::scoped_lock l{ lock_ };
        const auto       f = FindPlayerUnlocked(a_id);
        if (!f) {
            return false;
        }
        f->style = a_style;
        Api::Events::FactionChanged(a_id);
        return true;
    }

    bool Factions::SetColor(Id a_id, std::uint32_t a_color)
    {
        std::scoped_lock l{ lock_ };
        const auto       f = FindPlayerUnlocked(a_id);
        if (!f) {
            return false;
        }
        f->color = a_color & 0xFFFFFF;
        Api::Events::FactionChanged(a_id);
        return true;
    }

    Id Factions::FromLegacyStyle(Id a_style) const
    {
        std::scoped_lock l{ lock_ };
        for (const auto& f : predefined_) {
            if (f.style == a_style) {
                return f.id;
            }
        }
        return kNeutral;
    }

    void Factions::ClearPlayerFactions()
    {
        std::scoped_lock l{ lock_ };
        player_.clear();
        nextPlayer_ = 1;
    }

    // Formato do registro 'FACT' v1 (só facções do jogador; pré-definidas vêm do JSON):
    //   u32 nextPlayer, u32 count, depois count × { u32 id, u32 style, u32 color, u16 keyLen, key, u16 nameLen, name }.
    void Factions::Save(SKSE::SerializationInterface* a_intfc) const
    {
        std::scoped_lock l{ lock_ };
        if (player_.empty()) {
            return;
        }
        if (!a_intfc->OpenRecord(kRecord, kRecordVersion)) {
            logger::error("Cosave: falha ao abrir o registro FACT.");
            return;
        }
        const auto writeString = [&](const std::string& a_s) {
            const auto len = static_cast<std::uint16_t>(std::min<std::size_t>(a_s.size(), 0xFFFF));
            a_intfc->WriteRecordData(len);
            a_intfc->WriteRecordData(a_s.data(), len);
        };
        a_intfc->WriteRecordData(nextPlayer_);
        a_intfc->WriteRecordData(static_cast<std::uint32_t>(player_.size()));
        for (const auto& f : player_) {
            a_intfc->WriteRecordData(f.id);
            a_intfc->WriteRecordData(f.style);
            a_intfc->WriteRecordData(f.color);
            writeString(f.key);
            writeString(f.name);
        }
    }

    void Factions::LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version)
    {
        if (a_version != kRecordVersion) {
            logger::warn("Cosave: versão {} do registro FACT não suportada; facções do jogador perdidas.", a_version);
            return;
        }
        const auto readString = [&]() {
            std::uint16_t len{};
            a_intfc->ReadRecordData(len);
            std::string s(len, '\0');
            a_intfc->ReadRecordData(s.data(), len);
            return s;
        };

        const auto&      styles = Config::Get().Styles();
        std::scoped_lock l{ lock_ };
        std::uint32_t    count{};
        a_intfc->ReadRecordData(nextPlayer_);
        a_intfc->ReadRecordData(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            Faction f;
            f.player = true;
            a_intfc->ReadRecordData(f.id);
            a_intfc->ReadRecordData(f.style);
            a_intfc->ReadRecordData(f.color);
            f.key  = readString();
            f.name = readString();
            if (std::find(styles.begin(), styles.end(), f.style) == styles.end()) {
                logger::warn("Cosave: facção '{}' com estilo que não existe mais; usando o primeiro estilo.", f.name);
                f.style = styles.empty() ? kNeutral : styles.front();
            }
            player_.push_back(std::move(f));
        }
        logger::info("Cosave: {} facção(ões) do jogador carregada(s).", player_.size());
    }
}
