#include "World/Claims.h"

#include "Banner/BannerController.h"
#include "Api/Events.h"
#include "Core/I18n.h"
#include "Core/Timer.h"
#include "World/Conquest.h"
#include "World/Placement.h"

namespace AoS
{
    namespace
    {
        constexpr float kUnitsPerMeter = 70.0f;  // ~1,43 cm por unidade do jogo
        constexpr float kRadToDeg      = 57.2957795f;
        constexpr float kRayAbove      = 200.0f;  // começa um pouco acima do jogador (evita copas/tetos)
        constexpr float kRayBelow      = 800.0f;  // e desce até bem abaixo dele

        struct GroundHit
        {
            RE::NiPoint3 point;
            RE::NiPoint3 normal{ 0.0f, 0.0f, 1.0f };
        };

        // Raio vertical de cima para baixo em (x, y). Unidades do Havok = unidades do jogo × GetWorldScale().
        // nullopt se não acertou nada.
        std::optional<GroundHit> RaycastDown(RE::bhkWorld* a_world, float a_x, float a_y, float a_zTop, float a_zBottom)
        {
            const float     s = RE::bhkWorld::GetWorldScale();
            RE::bhkPickData pick{};
            pick.rayInput.from       = RE::hkVector4(a_x * s, a_y * s, a_zTop * s, 0.0f);
            pick.rayInput.to         = RE::hkVector4(a_x * s, a_y * s, a_zBottom * s, 0.0f);
            pick.rayInput.filterInfo = static_cast<std::uint32_t>(RE::COL_LAYER::kLOS);  // terreno + estáticos
            {
                RE::BSReadLockGuard lock{ a_world->worldLock };
                a_world->PickObject(pick);
            }
            if (!pick.rayOutput.HasHit()) {
                return std::nullopt;
            }
            alignas(16) float n[4];
            _mm_store_ps(n, pick.rayOutput.normal.quad);

            GroundHit hit;
            hit.point  = { a_x, a_y, a_zTop + (a_zBottom - a_zTop) * pick.rayOutput.hitFraction };
            hit.normal = { n[0], n[1], n[2] };
            if (hit.normal.z < 0.0f) {  // normal de face invertida: queremos a que aponta para cima
                hit.normal = { -n[0], -n[1], -n[2] };
            }
            return hit;
        }

        // Ângulos (x, y) que inclinam o "cima" do objeto até a normal, no referencial do heading do jogador.
        // Convenção Gamebryo: +X inclina o topo para a frente (+Y local), +Y inclina o topo para a esquerda.
        RE::NiPoint3 AlignToNormal(const RE::NiPoint3& a_n, float a_heading)
        {
            const float fwd   = a_n.x * std::sin(a_heading) + a_n.y * std::cos(a_heading);
            const float right = a_n.x * std::cos(a_heading) - a_n.y * std::sin(a_heading);
            return { std::atan2(fwd, a_n.z), -std::atan2(right, a_n.z), a_heading };
        }
    }

    Claims& Claims::Get() noexcept
    {
        static Claims instance;
        return instance;
    }

    void Claims::TryClaimHere()
    {
        ScopedTimer timer{ "Claims::TryClaimHere" };
        const auto& cfg    = Config::Get().Claim();
        const auto  player = RE::PlayerCharacter::GetSingleton();
        const auto  cell   = player ? player->GetParentCell() : nullptr;
        if (!cell || !player->Is3DLoaded()) {
            return;
        }
        if (cell->IsInteriorCell()) {
            I18n::NotifyTr("claim.interior");
            return;
        }
        if (static_cast<int>(claims_.size()) >= cfg.maxClaims) {
            I18n::NotifyTr("claim.limit", cfg.maxClaims);
            return;
        }

        const auto  ws      = player->GetWorldspace();
        const auto  wsId    = ws ? ws->GetFormID() : 0;
        const float heading = player->GetAngleZ();
        const auto  origin  = player->GetPosition();
        const float x       = origin.x + cfg.forwardDistance * std::sin(heading);
        const float y       = origin.y + cfg.forwardDistance * std::cos(heading);

        for (const auto& c : claims_) {
            const float dx = c.position.x - x, dy = c.position.y - y;
            if (c.worldspace == wsId && dx * dx + dy * dy < cfg.minDistance * cfg.minDistance) {
                I18n::NotifyTr("claim.tooClose", static_cast<int>(std::sqrt(dx * dx + dy * dy) / kUnitsPerMeter),
                               static_cast<int>(cfg.minDistance / kUnitsPerMeter));
                return;
            }
        }

        GroundHit hit{ { x, y, origin.z }, { 0.0f, 0.0f, 1.0f } };
        if (const auto world = cell->GetbhkWorld()) {
            const auto h = RaycastDown(world, x, y, origin.z + kRayAbove, origin.z - kRayBelow);
            if (!h) {
                I18n::NotifyTr("claim.noGround");
                return;
            }
            hit = *h;
        } else {
            logger::warn("Claims: célula sem bhkWorld; usando a altura do jogador e sem inclinação (fallback).");
        }

        const float slope = std::acos(std::clamp(hit.normal.z, -1.0f, 1.0f)) * kRadToDeg;
        if (slope > cfg.maxSlopeDegrees) {
            I18n::NotifyTr("claim.tooSteep", static_cast<int>(slope), static_cast<int>(cfg.maxSlopeDegrees));
            return;
        }

        // M8: o claim vira território da facção do jogador (cria a padrão se ainda não existe).
        const auto faction = Conquest::Get().EnsurePlayerFaction();
        if (!faction) {
            I18n::NotifyTr("claim.failed");
            return;
        }

        const auto base = cfg.banner.Resolve<RE::TESBoundObject>();
        if (!base) {
            logger::error("Claims: objeto do estandarte {}|0x{:X} não encontrado.", cfg.banner.plugin, cfg.banner.localId);
            I18n::NotifyTr("claim.failed");
            return;
        }
        const auto angle = AlignToNormal(hit.normal, heading);
        const auto ref   = Placement::PlaceAt(base, hit.point, angle, /*keyframed*/ true);
        if (!ref) {
            I18n::NotifyTr("claim.failed");
            return;
        }

        Claim c;
        c.serial     = nextSerial_++;
        c.id         = MakeId(std::format("claim/{}", c.serial));
        c.worldspace = wsId;
        c.position   = hit.point;
        c.ref        = ref->GetFormID();
        c.founder    = *faction;
        claims_.push_back(c);
        RegisterTerritory(c);
        SetTerritoryOwner(c.id, *faction);  // valida, loga e fica no TERR

        logger::info("Claims: #{} (0x{:08X}) em ({:.0f}, {:.0f}, {:.0f}) ws 0x{:08X}, normal ({:.2f}, {:.2f}, {:.2f}), "
                     "inclinação {:.1f}°, ângulos ({:.3f}, {:.3f}, {:.3f}), base '{}', ref 0x{:08X}",
                     claims_.size(), c.id, hit.point.x, hit.point.y, hit.point.z, wsId, hit.normal.x, hit.normal.y,
                     hit.normal.z, slope, angle.x, angle.y, angle.z, base->GetName(), c.ref);
        I18n::NotifyTr("claim.created", claims_.size(), cfg.maxClaims, static_cast<int>(slope));
        Api::Events::ClaimCreated(c.ref, claims_.size());
    }

    void Claims::RegisterTerritory(const Claim& a_claim)
    {
        Territory t;
        t.key         = std::format("claim/{}", a_claim.serial);
        t.id          = a_claim.id;
        t.name        = I18n::Tr("claim.territoryName", a_claim.serial);
        t.type        = TerritoryType::kClaim;
        t.hasPosition = true;
        t.position    = a_claim.position;
        t.worldspace  = a_claim.worldspace;
        if (!Config::Get().AddDynamic(std::move(t))) {
            return;
        }
        // Dono inicial; se o save tem TERR (gravado depois do CLAM), o valor salvo sobrescreve ao carregar.
        State::Get().SetOwner(a_claim.id, Factions::Get().IsValidOwner(a_claim.founder) ? a_claim.founder : kNeutral);
    }

    void Claims::Clear()
    {
        claims_.clear();
        nextSerial_ = 1;
        Config::Get().ClearDynamic();
    }

    // Formato do registro 'CLAM' v2:
    //   u32 nextSerial, u32 count, count × { u32 id, u32 worldspace, f32 x, f32 y, f32 z, u32 refFormId,
    //                                         u32 serial, u32 founderFaction }.
    // v1: igual, sem serial/founder (serial recuperado pelo hash; founder = facção do jogador).
    void Claims::Save(SKSE::SerializationInterface* a_intfc) const
    {
        if (claims_.empty()) {
            return;
        }
        if (!a_intfc->OpenRecord(kRecord, kRecordVersion)) {
            logger::error("Cosave: falha ao abrir o registro CLAM.");
            return;
        }
        a_intfc->WriteRecordData(nextSerial_);
        a_intfc->WriteRecordData(static_cast<std::uint32_t>(claims_.size()));
        for (const auto& c : claims_) {
            a_intfc->WriteRecordData(c.id);
            a_intfc->WriteRecordData(c.worldspace);
            a_intfc->WriteRecordData(c.position.x);
            a_intfc->WriteRecordData(c.position.y);
            a_intfc->WriteRecordData(c.position.z);
            a_intfc->WriteRecordData(c.ref);
            a_intfc->WriteRecordData(c.serial);
            a_intfc->WriteRecordData(c.founder);
        }
    }

    void Claims::LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version)
    {
        if (a_version != 1 && a_version != kRecordVersion) {
            logger::warn("Cosave: versão {} do registro CLAM não suportada; claims descartados.", a_version);
            return;
        }
        const auto playerFaction = Factions::Get().PlayerFaction();  // FACT é gravado antes do CLAM
        std::uint32_t count{};
        a_intfc->ReadRecordData(nextSerial_);
        a_intfc->ReadRecordData(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            Claim      c;
            RE::FormID ws{}, ref{};
            a_intfc->ReadRecordData(c.id);
            a_intfc->ReadRecordData(ws);
            a_intfc->ReadRecordData(c.position.x);
            a_intfc->ReadRecordData(c.position.y);
            a_intfc->ReadRecordData(c.position.z);
            a_intfc->ReadRecordData(ref);
            if (a_version >= 2) {
                a_intfc->ReadRecordData(c.serial);
                a_intfc->ReadRecordData(c.founder);
            } else {
                for (std::uint32_t n = 1; n < nextSerial_ && c.serial == 0; ++n) {  // v1: serial pelo hash do Id
                    if (MakeId(std::format("claim/{}", n)) == c.id) {
                        c.serial = n;
                    }
                }
                c.founder = playerFaction ? playerFaction->id : kNeutral;
            }
            if (!a_intfc->ResolveFormID(ref, c.ref)) {
                logger::warn("Cosave: estandarte do claim 0x{:08X} não existe mais; descartado.", c.id);
                continue;
            }
            if (ws != 0 && !a_intfc->ResolveFormID(ws, c.worldspace)) {
                logger::warn("Cosave: worldspace do claim 0x{:08X} não existe mais (plugin removido?); descartado.", c.id);
                continue;
            }
            claims_.push_back(c);
            RegisterTerritory(c);
        }
        logger::info("Cosave: {} claim(s) carregado(s) (CLAM v{}), registrados como territórios.", claims_.size(), a_version);
    }
}
