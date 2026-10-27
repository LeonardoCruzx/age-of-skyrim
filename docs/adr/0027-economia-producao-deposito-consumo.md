# 0027. Economia: produção data-driven, depósito da sede, saldos e consumo

- **Status:** Aceita
- **Data:** 2026-10-27
- **Origem:** Técnica

## Contexto
A M12 transforma territórios em recursos que vão para o baú da sede como itens reais
([ADR 0012](0012-estoque-itens-reais.md), [ADR 0017](0017-estoque-classificacao-por-keyword.md)). O baú da sede
(M6) está sendo feito em paralelo e ainda não existe; as facções de IA também precisam de recursos para a M16.

## Decisão
- **Dados:** `Economy.json` + `Economy/*.json` (outros mods; ordem alfabética, posterior substitui). Recurso =
  `{ id, category, item: "Plugin.esp|0xID" }`, onde `category` é uma categoria de `Resources.json` (só checada e
  logada: a classificação real continua sendo a do estoque ao contar o baú). Produção por hora por **tipo de
  território** (`production.<tipo>.<recurso>`) e multiplicador por **facção dona** (`factionMultiplier`,
  `"player"` = todas as do jogador). Item ausente = aviso no log e o recurso é ignorado.
- **Saldo por facção e recurso (`double`):** frações acumulam (0,25/h = 1 item a cada 4 h).
  - Facção do jogador: o saldo é o **pendente**; a parte inteira vira itens no depósito.
  - Facção de IA: o saldo é o estoque numérico (consultado pela M16 via `Economy::Balance`).
- **Depósito:** ponto de integração `Economy::SetDepot(RE::TESObjectREFR*)`, chamado pela sede (M6) na thread
  principal. Sem depósito, a produção fica pendente no cosave; ao definir, o pendente é entregue na hora.
  Entrega com `TESObjectREFR::AddObjectToContainer`, **uma chamada por item** (somando todas as facções do
  jogador), dentro do tick (que já roda via `AddTask`). O FormID do depósito também vai para o cosave.
- **Consumo (gancho M14):** `RegisterUpkeep(chave, provider)`; a cada tick cada provider lista
  `{ facção, recurso, por hora }`. Paga primeiro do saldo; para o jogador, o que faltar sai dos itens do
  depósito (`RemoveItem`, só lê o inventário se precisar). O que não for pago vira **déficit** consultável
  (`InDeficit`, `Deficit`, `DeficitHours` = horas seguidas em déficit) para moral/deserção na M14.
- **Cosave `ECON` v1:** último tick (horas, `double`), FormID do depósito, e por facção: horas de déficit e
  `(recurso Id, saldo, déficit)`. Recursos por Id (hash), então reordenar o JSON não corrompe; recurso removido é
  descartado no load.
- Valores padrão de exemplo (vila → pão/repolho/lenha, cidade → ouro, forte → ferro, acampamento → couro) são
  só ponto de partida de balanceamento.

## Consequências
- A sede (M6) só precisa chamar `SetDepot(baú)` ao criar/carregar a sede; nenhuma outra dependência.
- O mapa/MCM podem mostrar pendentes e saldos sem tocar no inventário.
- Mods de comida/minério entram adicionando um `Economy/*.json` com os itens deles.
- `AddObjectToContainer` num baú persistente de célula descarregada não foi testado no jogo (esperado: ok, como
  `AddItem` do Papyrus).
