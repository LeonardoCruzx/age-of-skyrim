# 0060. Lei e prisioneiros de guerra: contagem por facção (`LAWS`), sem atores

- **Status:** Aceita
- **Data:** 2026-10-28
- **Origem:** Técnica (M25)

## Contexto
A M25 pede prisioneiros de guerra (resgate, execução, prisão) e lei (recompensa/bounty) ao trocar o dono de um
território. Guerra Civil/mods de lei de terceiros não podem ser afetados sem o usuário pedir (ADR 0005). A M16/M17
(IA e diplomacia) já estavam em andamento quando a M25 começou; agora estão mescladas na `main` (`Ai::Resolve`
resolve combates e troca o dono via `SetTerritoryOwner`).

## Decisão
- **Sem atores:** `Law::Prisoners(faction)` é só uma contagem agregada por facção (quem os detém), como a
  guarnição numérica da M14. Cosave **`LAWS` v1**: `u32 n × { Id facção, u32 prisioneiros }`.
- **Captura (ganho):** `Law::CaptureFromCombat(capturingFaction, fromFaction, defeatedCount)` é o ponto de
  integração único. `defeatedCount × law.captureFraction` (padrão 50%) vira prisioneiro do vencedor, limitado à
  capacidade (`Law::Capacity`, ver ADR 0061); o resto é só baixa, sem efeito numérico adicional.
  - **M16 já mesclada:** `Ai::Resolve` (`src/Sim/Ai.cpp`) lê `Garrison::Size(território)` do defensor **antes**
    de `SetTerritoryOwner` (depois da troca o número já é a guarnição inicial do novo dono) e chama
    `Law::CaptureFromCombat(atacante, defensor, essa_guarnição)` logo após a conquista.
  - **Debug:** `Law::DebugCapture` (hotkey `law.lawCaptureHotkey`, padrão F1) simula `law.debugCaptureCount`
    capturas no território do jogador, sem depender de um ataque real ter acontecido.
- **Ações sobre os prisioneiros que uma facção já detém** (agregados, sem origem individual):
  - `Ransom` (resgate): custa `law.ransomCostPerPrisoner` de ouro por prisioneiro. Facção do jogador paga com
    itens reais do baú da sede (mesmo padrão de `Upgrades::Start`: `Stock::CategoryIndex("gold")` +
    `Stock::Take`); IA paga numericamente via `Economy::Spend` (gancho que a M16 já expõe).
  - `Execute` (execução): sempre log; efeito best-effort em `Stability::ReportUnrest` (M26) na própria facção
    que executa — brutalidade interna custa ordem pública, não é um ataque à facção derrotada (que nem é
    rastreada por prisioneiro individual).

## Consequências
- Nenhuma dependência de código da M16/M17 além de uma chamada (`Garrison::Size` + `Law::CaptureFromCombat`) em
  `Ai::Resolve`; se essa mescla não tivesse acontecido ainda, a mesma API funcionaria isolada via o hotkey de
  debug — o ponto de integração é uma função pública direta (`Law::CaptureFromCombat`), não uma indireção por
  registro de callback: `Law` é um singleton, então não há necessidade de inversão de controle, e o padrão já
  existe no código (`Garrison::Set`/`Add` documentam "ex.: resultado de combate, M16" do mesmo jeito).
- Sem diplomacia: capturar prisioneiros de um combate não declara guerra nem altera relações (isso já é decidido
  pela M17 antes do ataque acontecer).
