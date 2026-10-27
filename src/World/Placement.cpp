#include "World/Placement.h"

namespace AoS
{
    Placement& Placement::Get() noexcept
    {
        static Placement instance;
        return instance;
    }

    RE::TESObjectREFR* Placement::Find(Id a_key) const
    {
        RE::FormID id{};
        {
            std::scoped_lock l{ lock_ };
            const auto it = refs_.find(a_key);
            if (it == refs_.end()) {
                return nullptr;
            }
            id = it->second;
        }
        const auto ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(id);
        return ref && !ref->IsDeleted() ? ref : nullptr;
    }

    std::size_t Placement::EnsurePlaced(const Territory& a_territory)
    {
        std::size_t failed = 0;
        for (const auto& set : a_territory.banners) {
            for (const auto& spec : set.placed) {
                if (Find(spec.key)) {
                    continue;
                }
                if (const auto ref = PlaceNearPlayer(spec)) {
                    std::scoped_lock l{ lock_ };
                    refs_[spec.key] = ref->GetFormID();
                } else {
                    ++failed;
                }
            }
        }
        return failed;
    }

    RE::TESObjectREFR* Placement::PlaceNearPlayer(const PlacedSpec& a_spec)
    {
        const auto player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->GetParentCell() || !player->Is3DLoaded()) {
            logger::warn("Placement: jogador sem célula carregada; tente de novo fora de menus/loading.");
            return nullptr;
        }
        const auto base = a_spec.base.Resolve<RE::TESBoundObject>();
        if (!base) {
            logger::error("Placement: objeto base {}|0x{:X} não encontrado.", a_spec.base.plugin, a_spec.base.localId);
            return nullptr;
        }

        const auto ref = player->PlaceObjectAtMe(base, /*forcePersist*/ true);
        if (!ref) {
            logger::error("Placement: PlaceObjectAtMe falhou para 0x{:08X}.", base->GetFormID());
            return nullptr;
        }

        // Offset no referencial do jogador: x = direita, y = frente (heading = rotação em Z).
        const float  h   = player->GetAngleZ();
        const auto   o   = a_spec.offset;
        RE::NiPoint3 pos = player->GetPosition();
        pos.x += o.x * std::cos(h) + o.y * std::sin(h);
        pos.y += -o.x * std::sin(h) + o.y * std::cos(h);
        pos.z += o.z;
        ref->SetPosition(pos);

        logger::info("Placement: criado 0x{:08X} ('{}') a partir de {}|0x{:X}.", ref->GetFormID(),
                     base->GetName(), a_spec.base.plugin, a_spec.base.localId);
        return ref.get();
    }

    RE::TESObjectREFR* Placement::PlaceAt(RE::TESBoundObject* a_base, const RE::NiPoint3& a_pos,
                                          const RE::NiPoint3& a_angle, bool a_keyframed)
    {
        const auto player = RE::PlayerCharacter::GetSingleton();
        if (!a_base || !player || !player->GetParentCell() || !player->Is3DLoaded()) {
            return nullptr;
        }
        const auto ref = player->PlaceObjectAtMe(a_base, /*forcePersist*/ true);
        if (!ref) {
            logger::error("Placement: PlaceObjectAtMe falhou para 0x{:08X}.", a_base->GetFormID());
            return nullptr;
        }
        ref->data.angle = a_angle;  // SetPosition reaplica data.angle junto com a posição
        ref->SetPosition(a_pos);
        if (a_keyframed && !ref->SetMotionType(RE::TESObjectREFR::MotionType::kKeyframed)) {
            // 3D recém-criado pode ainda não existir: tenta de novo no próximo frame.
            SKSE::GetTaskInterface()->AddTask([id = ref->GetFormID()] {
                if (const auto r = RE::TESForm::LookupByID<RE::TESObjectREFR>(id)) {
                    r->SetMotionType(RE::TESObjectREFR::MotionType::kKeyframed);
                }
            });
        }
        return ref.get();
    }

    void Placement::Clear()
    {
        std::scoped_lock l{ lock_ };
        refs_.clear();
    }

    // Formato do registro 'PLAC' v1:  u32 count, depois count × { u32 key, u32 formId }.
    void Placement::Save(SKSE::SerializationInterface* a_intfc) const
    {
        std::scoped_lock l{ lock_ };
        if (refs_.empty()) {
            return;
        }
        if (!a_intfc->OpenRecord(kRecord, kRecordVersion)) {
            logger::error("Cosave: falha ao abrir o registro PLAC.");
            return;
        }
        a_intfc->WriteRecordData(static_cast<std::uint32_t>(refs_.size()));
        for (const auto& [key, formId] : refs_) {
            a_intfc->WriteRecordData(key);
            a_intfc->WriteRecordData(formId);
        }
    }

    void Placement::LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version)
    {
        if (a_version != kRecordVersion) {
            logger::warn("Cosave: versão {} do registro PLAC não suportada; objetos serão recriados.", a_version);
            return;
        }

        std::scoped_lock l{ lock_ };
        std::uint32_t    count{};
        a_intfc->ReadRecordData(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            Id         key{};
            RE::FormID oldId{}, newId{};
            a_intfc->ReadRecordData(key);
            a_intfc->ReadRecordData(oldId);
            if (a_intfc->ResolveFormID(oldId, newId)) {
                refs_[key] = newId;
            }
        }
        logger::info("Cosave: {} objeto(s) criado(s) em runtime carregado(s).", refs_.size());
    }
}
