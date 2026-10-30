# 0071. Resposta da MessageBox vanilla é o índice do botão, não "4 + índice"

- **Status:** Aceita
- **Data:** 2026-10-30
- **Origem:** Bug de usuário (menu de debug da M30: itens abriam a opção errada, "Fechar" não fechava)

## Contexto
`UI/Prompt.cpp` (base de toda caixa de diálogo do mod: sede M6, governador M24, mapa tático sem PrismaUI
M11, menu de debug M30) convertia o valor recebido em `IMessageBoxCallback::Run` assumindo
"4 + índice do botão" — suposição tirada de outros plugins, nunca confirmada no jogo (o comentário dizia
"o valor bruto vai para o log para conferir no teste").

O log do primeiro teste do menu de debug confirmou o contrário — o valor é o **próprio índice**:
- caixa de categorias (8 botões: 6 categorias + "Mais..." + "Fechar"): "Fechar" (índice 7) chegou como `7`
  e o código abria a categoria **D**; "Mais..." (`6`) abria a **C**;
- subcategorias de 2–3 botões: os cliques chegavam como `0`/`1`, que o código tratava como "sem escolha"
  — nenhuma ação rodava.

O mesmo bug explica a sede (M6) nunca ter sido fundada em nenhum teste desta sessão: o log mostrava
"Prompt: resposta bruta 0 -> botão -1. Sede: jogador recusou." — o jogador clicava **Sim** (índice 0) e o
mod entendia recusa.

## Decisão
`Callback` passa a receber o número de botões da caixa; valor bruto `0..N-1` = índice do botão, qualquer
outro = `kNoChoice`. Nenhum chamador muda (todos já usavam índice a partir de 0).

Junto: no menu de debug, escolher uma ação agora roda e **fecha** o menu (antes reabria as categorias).
Várias ações abrem a própria caixa (simular conquista → pergunta da sede; governador; mapa tático) e
reabrir o menu ao mesmo tempo colocaria duas caixas na fila. "Voltar" continua voltando às categorias.

## Consequências
- Sede, governador, mapa tático (menus vanilla) e menu de debug passam a responder ao botão clicado.
- Se algum dia a MessageBox devolver um valor fora da faixa (ex.: fechada por outro meio), cai em
  `kNoChoice` em vez de um índice errado.
