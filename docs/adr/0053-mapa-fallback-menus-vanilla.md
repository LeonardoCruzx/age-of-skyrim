# 0053. Mapa tático sem PrismaUI: cadeia de MessageBox vanilla

- **Status:** Aceita
- **Data:** 2026-10-28
- **Origem:** Técnica (M11, [ADR 0013](0013-mesa-de-guerra-mapa-tatico.md))

## Contexto
Sem PrismaUI a mesa de guerra ainda precisa das ações essenciais, sem ESP, Papyrus nem SkyUI.

## Decisão
- `UI/WarMapMenus` usa o `Prompt::Ask` (MessageBox vanilla com botões, ADR 0023): cada resposta abre a próxima caixa.
- Telas: **principal** (resumo: seus territórios, rotas × limite, alvos, estoque da sede) → **listas paginadas**
  (`warMap.pageSize`, padrão 6, máx. 7 + "Mais…" + "Voltar"): territórios próximos (do jogador em exterior, ou da
  sede em interior, até `warMap.nearbyRadius`), seus territórios, alvos, rotas → **painel do território** (tipo,
  província, dono, rotas e renda, sede/alvo, camadas extras) → ações.
- **Criar rota com 2 escolhidos:** "Iniciar rota aqui" guarda a ponta A; no painel de outro território seu aparece
  "Criar rota de A até aqui". Remover rota pelo painel da rota. Marcar/desmarcar alvo em territórios que não são seus.
- As ações chamam as mesmas funções da view (`WarMap::CreateRoute/RemoveRoute/SetTarget`), que só aceitam
  territórios/rotas da facção do jogador.

## Consequências
- Navegação mais lenta que o mapa, mas completa; nenhuma dependência nova.
- Ponto a verificar no jogo: limite de botões do MessageBox (até 9 usados por tela).
