# 0068. Casas do jogador como candidatas a sede (M29)

- **Status:** Aceita
- **Data:** 2026-10-30
- **Origem:** Pedido do usuário, testando no Alternate Start em Lakeview Manor (Falkreath)

## Contexto
Até aqui, candidatas a sede (M6/ADR 0023) eram só fortes/torres (`Seats/vanilla.json → auto.types`), e o
único jeito de uma virar território do jogador era a conquista por local limpo (M5) — que nunca dispara pra
uma casa do jogador, porque ela nunca tem inimigo pra limpar (o evento `LocationCleared` não existe pra esse
caso). O usuário pediu pra permitir casas como Lakeview Manor como base inicial, **exceto as que ficam dentro
de outra cidade** (Breezehome em Whiterun, Honeyside em Riften, Hjerim em Windhelm, Vlindrel Hall em Markarth,
Proudspire Manor em Solitude — essas já são "território" da própria cidade, não fazem sentido como sede
separada).

Achei a keyword vanilla certa pra isso: `LocTypePlayerHouse`, aplicada pela Bethesda a qualquer casa que o
jogador pode comprar/construir (as 5 de cidade + as 3 do Hearthfire — Lakeview Manor, Windstad Manor,
Heljarchen Hall — e, por convenção, casas de outros mods que seguem o mesmo padrão). Isso cobre exatamente o
pedido ("todas as possíveis casas") sem precisar listar FormIDs um por um.

**Por que as de dentro de cidade saem sozinhas:** a descoberta (M4, `Discovery::Scan`) já remove sub-locais —
qualquer local cuja cadeia de locais (`parentLoc`) passa por outro candidato descoberto não conta como
território próprio (ex.: hoje já é assim entre uma cidade e seus bairros). Breezehome etc. têm a cidade como
ancestral na cadeia de locais (convenção da Bethesda pra jurisdição/bounty/viagem rápida), e cidade já é um
tipo descoberto por padrão — então, ao adicionar "house" na lista de tipos descobertos, essas ficam de fora
pela MESMA lógica que já existia, sem código novo de exclusão. **Risco aceito:** não dá pra confirmar isso sem
abrir o xEdit (fora do alcance deste ambiente); se alguma casa de cidade escapar dessa exclusão automática, o
usuário só precisa colocá-la em `discovery.exclude` ou `Seats/*.json → exclude` — não é um crash, é só uma
candidata indesejada na lista.

**Segundo problema, maior:** mesmo descoberta como território, uma casa nunca passaria a ser do jogador pelo
caminho normal (conquista por local limpo) — ela não tem inimigo. Mas ela também não devia precisar: é sua
desde que você entra. Decisão: ao entrar numa casa (sink `TESActorLocationChangeEvent`, já existente em
`Conquest::OnPlayerLocationChanged`, usado antes só pra guardar o local anterior), se ela for território tipo
"house" e ainda estiver neutra, ela passa pra facção do jogador direto — reaproveitando
`Conquest::ConquerForPlayer` (cria a facção se faltar, respeita `conquest.enabled`/`exclude`), só com uma nova
`Source::kHome` pra não dizer "conquistada" (mensagem própria: "é sua casa — agora pertence a X"). Isso
dispara o listener do `Seat` (M6) do jeito normal, perguntando se quer fundar a sede ali.

## Decisão
1. Novo `TerritoryType::kHouse` (`Config.h`); `ToString`/`ParseTerritoryType` com `"house"`.
2. `Discovery.cpp`: nova `TypeKeyword{ "LocTypePlayerHouse", TerritoryType::kHouse }`; `settings.json →
   discovery.types` passa a incluir `"house"` por padrão.
3. `Conquest::OnPlayerLocationChanged` (antes só guardava `previous_`): ao entrar numa casa neutra, chama
   `ConquerForPlayer(..., Source::kHome)` direto — sem esperar `LocationCleared`.
4. `Seat::LoadFile`: a regra `"auto"` trata `"house"` fora do teto `perProvince` — toda casa descoberta entra
   como candidata, sem concorrer por posição com fortes/torres (só teria 1 casa por província mesmo, mas não
   custa nada deixar explícito). `Seats/vanilla.json → auto.types` passa a incluir `"house"`.
5. `Ai::CanTarget`: território tipo `"house"` nunca é alvo da IA (nem pra expandir em território neutro, nem
   pra atacar — casa do jogador não é fortificação, não faz sentido como objetivo militar).

Alternativas descartadas:
- **Listar as 3 casas do Hearthfire por FormID fixo em `Seats/vanilla.json → candidates`:** resolveria só pra
  elas, não pra "todas as possíveis casas" como o usuário pediu, e não cobriria casas de outros mods.
- **Exigir uma ação explícita do jogador (nova hotkey) pra "reivindicar" a própria casa:** mais uma tecla de
  debug pra decorar, por um caso que já tem sinal claro e seguro (location change pra um tipo "house" neutro).
  Entrar na casa já é a ação natural; o prompt de "fundar sede aqui?" (M6, `askBeforeFounding`) já dá a chance
  de dizer não.

## Consequências
- Entrar numa casa-tipo-"house" ainda neutra, com `conquest.enabled` ligado, sempre a transfere pra facção do
  jogador (criando-a se for a primeira conquista) e oferece fundar a sede ali — comportamento novo, pode
  surpreender quem tem várias casas e só queria roleplay sem o lado de facção do mod; mitigado por ser
  **opt-out fácil**: `conquest.exclude`/`discovery.exclude` por casa, ou `conquest.enabled: false` pra desligar
  tudo.
- Território tipo "house" participa de melhorias (`Upgrades.json`, grupo "todos os tipos") e guarnição do
  mesmo jeito que fort/tower/camp/vila/cidade — não restringido further (fora do escopo deste pedido).
- Nenhuma mudança de formato de cosave (`TERR` continua v2); território novo entra pelo caminho que já existe.
