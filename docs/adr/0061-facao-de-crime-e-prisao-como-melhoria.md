# 0061. Facção de crime vanilla do Hold (bounty) e prisão como melhoria de território

- **Status:** Aceita
- **Data:** 2026-10-28
- **Origem:** Técnica (M25)

## Contexto
`ISSUE-GAME-02` do comparativo (`docs/COMPARATIVO-CONQUEST-OF-SKYRIM.md` seção 2): a recompensa (bounty) da
facção antiga continua depois de trocar o dono de uma cidade. A M25 pede essa limpeza, **configurável** e com o
padrão desligado (ADR 0005: zero interferência com mods de Guerra Civil/lei por padrão).

## Decisão
- **Facção de crime = `RE::BGSLocation::unreportedCrimeFaction` do Hold** (campo `FNAM`, documentado no
  CommonLibSSE-NG, `extern/CommonLibSSE-NG/include/RE/B/BGSLocation.h`). **Não é um FormID de memória**: é a
  própria facção que o jogo usa para o bounty daquele Hold (ex.: `WhiterunFaction`), resolvida em runtime a
  partir do `BGSLocation*` do território via `Discovery::Describe` (reaproveitado, já usado no `Config::Load`
  para calcular `Territory.province`). Suposição assumida (não verificada em todos os Holds de todos os mods):
  para locais de mods de terceiros sem Hold reconhecido, ou sem esse campo preenchido, a ação é um no-op com
  aviso no log — nunca crash.
- **Ação:** `RE::PlayerCharacter::GetSingleton()->ClearAllCrimeGold(faction)` (método público documentado do
  CommonLibSSE-NG, índice de vtable `12D`), chamado em `Law::OnTerritoryOwnerChanged`, pendurado **ao lado** de
  `Realm::OnTerritoryOwnerChanged` no mesmo ponto único de troca de dono (`BannerController::SetTerritoryOwner`),
  sem concorrer com o gancho do Realm (dois `if (changed) { ... }` independentes).
- **Interruptor:** `settings.json -> law.touchCrimeFaction` (padrão **false**) é o mestre; `law.resetBounty`
  (padrão true) só tem efeito com ele ligado. Com o padrão, o mod nunca toca a recompensa do jogador — total
  compatibilidade com mods de Guerra Civil/lei de terceiros (ADR 0005) até o usuário ligar.
- **Prisão como melhoria:** `Upgrades.json` ganha a entrada `"prison"` (tipos fort/city/unknown, 3 níveis,
  `stats.prisonCapacity`). `Stat::kPrisonCapacity` é **aditivo, no fim** do enum `Stat` (`src/Sim/Upgrades.h`):
  os níveis construídos são persistidos como pares `(melhoria, nível)` no registro `UPGR`, nunca por índice
  bruto do enum (ver `Upgrades::Save`), então crescer o enum não quebra saves antigos — igual ao padrão já usado
  por `Modifier::Stability` (ADR 0062). `Law::Capacity(faction)` soma `law.baseCapacityPerTerritory` +
  `Upgrades::Stat(kPrisonCapacity)` de cada território da facção.

## Consequências
- Sem ESP: nenhum form de bounty é criado ou editado; só a chamada de limpeza no form vanilla já carregado.
- Teste no jogo precisa confirmar que `unreportedCrimeFaction` resolve para a facção certa num Hold vanilla
  (ex.: Branco Rio) com `law.touchCrimeFaction: true` — está documentado como suposição a confirmar no relatório
  final, não como fato garantido para 100% dos Holds/mods.
