# 0028. Rotas de comércio: renda por distância/tipo e corte por território inimigo

- **Status:** Aceita
- **Data:** 2026-10-27
- **Origem:** Técnica

## Contexto
A M13 cria rotas entre territórios que rendem ouro, simuladas fora da tela (sem caravanas no MVP). A diplomacia
(M17) ainda não existe e a mesa de guerra (M11) / MCM (M10) vão criar as rotas depois.

## Decisão
- **Rota** = dupla não ordenada de territórios; Id = hash de `route/<menor>/<maior>` (a mesma dupla nunca
  repete). O dono é o da ponta A, avaliado a cada tick: se as pontas mudam de dono a rota fica **inativa**
  (não é apagada) e volta a render se o dono se reunificar.
- **Quem comercia:** `CanTrade(a, b)` — hoje só a mesma facção (nunca neutro). A M17 troca a implementação.
- **Renda/h** = `(baseGoldPerHour + goldPer10000Units × distância/10000) × média(typeMultiplier das pontas)`,
  distância em XY entre as posições (`Territory.position`). Creditada no recurso `routes.resource` ("gold") do
  dono, pelo mesmo caminho da produção (jogador → depósito; IA → saldo).
- **Corte:** a rota não rende no tick se algum território de uma facção com quem o dono **não** pode comerciar
  (neutros não contam) está a menos de `blockRadius` do **segmento** A–B (distância ponto-segmento em XY, mesmo
  worldspace). É uma aproximação barata do "caminho": O(territórios) por rota, sem navmesh nem grafo.
  Descartado: caminhar pelos `neighbors` (o grafo k-NN não é conexo nem segue estradas, e daria falsos cortes).
- **Limites:** `maxRoutes` (padrão 8) e `maxDistance` (0 = sem limite) em `Economy.json → routes`.
- **API interna:** `Routes::Create/Remove/List/StatusOf` (resultado enumerado com chave de tradução
  `route.*`), thread-safe, para M10/M11.
- **Cosave `ROUT` v1:** contagem + `(territórioA, territórioB)`; rota com território inexistente é descartada.
- **Debug (F7):** liga os 2 territórios mais próximos do jogador com o mesmo dono; se não houver, passa os 2
  mais próximos para a facção do jogador (criando uma de teste, como o autoteste) via `SetTerritoryOwner` —
  só para teste.

## Consequências
- Custo por tick: rotas × territórios contas simples (dezenas × centenas), bem abaixo de 1 ms.
- Rotas longas que contornam território inimigo por estrada podem ser cortadas mesmo assim (linha reta);
  ajustável por `blockRadius`. Caravanas visíveis e caminho real ficam como extensão futura.
