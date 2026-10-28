# Snapshot do mapa tático (M11) — schema v1

O DLL (`WarMap::BuildSnapshot`, `src/UI/WarMap.cpp`) monta um JSON e envia para a view do PrismaUI com
`InteropCall("aosWarMapData", json)`, só ao abrir e quando algo muda ([ADR 0052](adr/0052-snapshot-json-do-mapa-e-camadas.md)).
O schema formal (subconjunto de JSON Schema) está em [`warmap-schema.json`](warmap-schema.json) e é conferido por
`tests/test_warmap_schema.py`. Exemplo: `python3 tools/warmap_sample.py --json exemplo.json`.

Ids = texto hex de 8 dígitos maiúsculos (`MakeId`); `null` = neutro/sem valor. Posições em unidades do jogo.

| Campo | Tipo | Conteúdo |
|---|---|---|
| `schema` | int | `1` (a view recusa outras versões) |
| `lang` | texto | idioma escolhido (`en`, `pt-br`) |
| `i18n` | objeto | chave → texto de todas as chaves `warmap.*` (com `{}` para a view preencher) |
| `factions[]` | `{id, name, color "#RRGGBB", player}` | todas as facções (pré-definidas e do jogador) |
| `playerFaction` | id/null | primeira facção do jogador |
| `territories[]` | ver abaixo | todos (JSON, descobertos e claims) |
| `provinces[]` | `{id, name}` | províncias (Holds) que aparecem nos territórios |
| `worldspaces[]` | `{id, name, minX, minY, maxX, maxY, count}` | limites dos territórios com posição, por worldspace |
| `worldspace` | id/null | worldspace mostrado ao abrir (o da sede, senão o com mais territórios) |
| `routes[]` | `{id, a, b, owner, active, blocked, blocker, distance, goldPerHour}` | rotas (M13) e status atual |
| `seat` | objeto/null | `{territory, stock[{key, name, count}], unclassified}` (última contagem do baú) |
| `player` | objeto/null | `{x, y, ws}` do jogador em exterior |
| `layers[]` | `{key, label}` | camadas registradas por `WarMap::AddProvider` |
| `limits` | `{maxRoutes, maxTargets}` | limites atuais |
| `buildMs` | número | tempo de montagem no DLL |

Território: `{id, key, name, type, province, owner, x, y, ws, neighbors[], seat, target, layers{}}` — `type` ∈
`fort|tower|camp|village|city|claim|unknown`; `x/y/ws` são `null` sem posição; `layers` = `{chave: valor}` só das
camadas com dado para ele.

Mensagens da view para o DLL (`window.aosWarMapAction(JSON)`): `{"action":"createRoute","a":id,"b":id}`,
`{"action":"removeRoute","route":id}`, `{"action":"target","territory":id,"on":bool}`, `{"action":"close"}`,
`{"action":"ready"}`. Resposta de ação: `aosWarMapResult({"ok":bool,"text":"já traduzido"})` seguido de um
snapshot novo.
