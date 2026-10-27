# 0020. Melhorias de territórios (custo + tempo de obra, visual por nível)

- **Status:** Aceita
- **Data:** 2026-10-27
- **Origem:** Decisão do usuário

## Contexto
Conquistar fortes e torres precisa ter consequência além de pintar o mapa: o jogador deve poder investir nos
locais tomados para que fiquem mais fortes ou mais úteis, dando uso ao estoque da sede e à economia.

## Decisão
- Melhorias por tipo de local, definidas em `Upgrades.json` (outros mods podem adicionar), com níveis I → II → III.
  Exemplos: muralhas, quartel, torre de vigia, armazém, mercado, forja.
- **Construção = custo + tempo de obra:** paga com itens reais do baú da sede (categorias da M7) e leva horas de
  jogo; enquanto está em obra, o local fica mais vulnerável. A obra é conferida no tick por hora da economia
  (nada de timer contínuo).
- **Visual por nível:** cada nível habilita objetos extras no local (barricadas, tendas, braseiros, estandartes),
  criados em runtime (ADR 0007), com coordenadas curadas por local em JSON. Locais sem coordenadas recebem só os status.
- Os efeitos são **status numéricos** no DLL (defesa, capacidade de estoque, renda, visão, guarnição máxima), lidos
  pela guarnição, economia, rotas, mapa e IA. Registro de cosave `UPGR` v1.
- Entra no roadmap como **M15**, depois da economia (M12), rotas (M13) e guarnição (M14), para os efeitos terem
  onde agir; IA, diplomacia e release viram M16–M18.

## Consequências
- Coordenadas visuais só existem para locais curados; patches de compatibilidade (M18) precisam ajustá-las
  quando outro mod muda o layout de um forte.
- Balanceamento de custos/tempos fica todo em JSON, ajustável sem recompilar.
- Textos de melhorias e níveis em en.json e pt-br.json.
