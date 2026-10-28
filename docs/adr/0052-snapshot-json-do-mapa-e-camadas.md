# 0052. Mapa tático: um snapshot JSON por envio, só ao abrir e quando muda, e camadas por provedor

- **Status:** Aceita
- **Data:** 2026-10-28
- **Origem:** Técnica (M11)

## Contexto
A view precisa de territórios, donos, províncias, vizinhança, rotas, estoque da sede e dados de módulos que ainda
não existem (guarnição M14, ameaças M16…). Regra do projeto: nada de polling, operações < 5 ms (ADR 0005).

## Decisão
- **Um snapshot completo** (`WarMap::BuildSnapshot`, com `ScopedTimer`) por envio, em vez de diffs: com ~300
  territórios são ~120 KB e poucos ms, e a view fica sem estado para sincronizar. Schema versionado (`schema: 1`)
  em [`docs/warmap-schema.json`](../warmap-schema.json) (descrição em [`docs/warmap-schema.md`](../warmap-schema.md)),
  validado por `tests/test_warmap_schema.py` (dados de exemplo + checagem estática dos campos escritos no C++).
- Ids vão como texto hex de 8 dígitos; posições em unidades do jogo; a view normaliza por worldspace (limites
  enviados em `worldspaces`). Textos: todas as chaves `warmap.*` do idioma atual vão em `i18n` (mesmos JSONs do
  mod, `I18n::WithPrefix`) e a view formata os `{}`.
- **Quando envia:** ao abrir; depois de cada ação da view; na conquista (listener da M5); e quando outro módulo
  chama `WarMap::NotifyChanged()` (coalescido numa task). Com a view fechada nada é montado.
- **Camadas**: `WarMap::AddProvider(chave, chaveDoRótulo, fn)`; `fn(const Territory&)` devolve um JSON
  (objeto/número/texto) ou `null`. Sai em `territories[].layers[chave]` e na lista `layers` (rótulo traduzido);
  a view e os menus vanilla mostram sem conhecer o módulo. O mapa não depende de M14/M16/M9.
- Meta de performance registrada no `tools/perf_report.py` (`WarMap::BuildSnapshot` = tick, 5 ms).

## Consequências
- Provedores rodam na thread principal, uma vez por território por envio: precisam ser O(1).
- Saves com muitos mods de locais (milhares de territórios) aumentam o snapshot linearmente; se preciso, filtro
  por worldspace no futuro.

## Atualização (merge com M14/M15)
Duas camadas registradas em `kDataLoaded`, depois de `Garrison::Load`/`Upgrades::Load`:
- `garrison`: `soldiers`/`max`/`present` de `Garrison::Get()`.
- `upgrades`: `defense`/`income`/`inConstruction` de `Upgrades::Get().Stat(...)`.

Chaves de tradução `warmap.layer.garrison`/`warmap.layer.upgrades` (en/pt-br).

## Atualização (merge com M16/M17)
Terceira camada registrada em `kDataLoaded`, depois de `Ai::Load`: `threats` (`Ai::Threats(t.id)`), com
`attacks` (quantidade) e `hoursLeft` (a mais próxima); sem ameaças, o provedor devolve `null` (convenção já
usada pelo WarMap para "sem dado neste território", diferente de um objeto vazio).
