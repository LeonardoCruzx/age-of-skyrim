# 0036. Governo: doutrinas, cargos e governadores como dados + bônus numéricos

- **Status:** Aceita
- **Data:** 2026-10-28
- **Origem:** Técnica (M18)

## Contexto
A M18 pede doutrinas (feudal, militar, república, mercantil), cargos (mordomo, comandante, ferreiro) e
governadores por província. A sede (M6) e a economia (M12) ainda não existem neste ponto, então os cargos não têm
onde ficar e a troca de doutrina não tem de onde tirar o custo.

## Decisão
- `Doctrines/*.json` (pasta extensível, ordem alfabética; **id repetido num arquivo posterior substitui** o anterior,
  para patches de balanço) com `doctrines`, `offices`, `governor` e `defaultDoctrine`. Cada doutrina/cargo é um conjunto
  de bônus **aditivos** (`production`, `tax`, `trade`, `recruitCost`, `recruitSpeed`, `populationGrowth`, `defense`,
  `upkeep`) e textos por chave de tradução (`nameKey`) ou `{en, pt-br}`.
- **Doutrina:** facção sem escolha usa a `defaultDoctrine` (vale para facções de IA). A **1ª escolha é livre** (fundação);
  depois, troca custa `changeCost` da doutrina nova e espera `settings.json → realm.doctrineCooldownHours` (padrão 168 h).
  `requiresTitle` trava doutrinas até um título (ADR 0038).
- **Pagamento por gancho:** `Government::SetPaymentHandler(fn(facção, custo) -> bool)`; a economia (M12) registra a
  função que debita o baú da sede. Sem gancho, a troca é grátis (logado). O gancho roda fora do mutex do reino.
- **Cargos:** `AssignOfficer(facção, cargo, npcBase = 0)` registra o ocupante e o bônus vale na hora. Vagas =
  `unlocks.officers` do título. **Colocar o NPC na sede fica para depois da M6**: o FormID do NPC já vai no cosave,
  e a futura função de spawn (base vanilla com FaceGen, outfit e pacotes nossos — lições do comparativo) só lê isso.
- **Governadores:** `AssignGovernor(facção, província)` exige **mais que `governor.majority`** (padrão 50%) dos
  territórios da província; o bônus regional vale nos territórios dela (`Modifiers::GetFor`). Ao perder a maioria
  (troca de dono) o governador sai, com aviso na tela. Vagas = `unlocks.governors`.
- Cosave `GOVT` v1: relógio do reino (ADR 0039) + por facção doutrina, última troca, cargos e governadores
  (FormIDs passam por `ResolveFormID`; NPC de plugin removido = vaga sem NPC).

## Consequências
- M10/M11 (MCM/mapa) só chamam `SetDoctrine/AssignOfficer/AssignGovernor` e mostram o `Result`.
- A escolha "na fundação" depende da UI de criação de facção (M10): até lá vale a doutrina padrão.
- NPCs de cargo/governador na sede são trabalho da M6/M20 sobre os dados já salvos.
