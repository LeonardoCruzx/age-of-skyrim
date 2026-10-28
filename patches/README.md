# Patches de compatibilidade

Cada pasta `patches/<nome>/` é um patch opcional do instalador FOMOD. O conteúdo da pasta espelha a pasta
`Data/` do jogo e é copiado como está (arquivos `.md` ficam de fora). Decisões em
[ADR 0041](../docs/adr/0041-fomod-layout.md) (instalador) e [ADR 0042](../docs/adr/0042-patches-de-compatibilidade.md) (formato).

```
patches/<nome>/
  SKSE/Plugins/AgeOfSkyrim/Compat/<nome>.json        exclusões e ajustes condicionais (abaixo)
  SKSE/Plugins/AgeOfSkyrim/Territories/<nome>.json   territórios/sedes (formato de Territories/, já suportado)
```

Para adicionar um patch:
1. Crie `patches/<nome>/` com os arquivos acima (nome em minúsculas, com hífens).
2. Adicione um `<plugin>` em `fomod/ModuleConfig.xml` com `<folder source="patch_<nome>" destination="" />`.
3. Rode `python3 -m unittest discover -s tests` (confere o XML, o JSON e que pasta e `<plugin>` batem).

## `Territories/<nome>.json` (já funciona)

O mesmo formato de `Data/SKSE/Plugins/AgeOfSkyrim/Territories/*.json`. Um território com `"location"` se liga a
um `BGSLocation` e sobrescreve só o que declarar (nome, tipo, província, posição). É assim que um patch de JK's,
Lux ou Open Cities corrige um local cujo mod mudou a geometria. Quando a M6 sair, o bloco `"seat"` (baú e mesa
de guerra, posição + rotação) entra no mesmo território.

```jsonc
{ "territories": [
  { "id": "meu_forte", "location": "Skyrim.esm|0x0ABCDE", "type": "tower" }
] }
```

## `Compat/<nome>.json` (lido pelo DLL desde a M16, `Config::LoadCompat`)

```jsonc
{
  "name": "civil-war-mods",
  // Patch ativo se QUALQUER plugin da lista estiver carregado. Lista vazia/ausente = sempre ativo.
  // Plugin ausente ou nome errado só desativa o patch (aviso no log), nunca dá erro.
  "whenLoaded": ["Algum Mod.esp"],
  // Só documentação (o loader ignora): origem de cada nome e se foi conferido.
  "plugins": [{ "plugin": "Algum Mod.esp", "mod": "Nome no Nexus", "kind": "civil-war", "verified": false }],
  // Mesmas chaves dos blocos de settings.json. Listas são SOMADAS (união) às do settings.json;
  // escalares (true/false, números) só valem se o settings.json não tiver o valor definido pelo usuário.
  "discovery": { "exclude": ["Plugin.esp|0xID"] },            // locais que não viram território
  "conquest":  {
    "exclude": ["Plugin.esp|0xID"],                            // locais que não podem ser conquistados
    "excludeFromPlugins": ["Algum Mod.esp"],                   // todo BGSLocation definido por estes plugins
                                                               // (sinônimo: "excludePlugins", como no settings.json)
    "skipCivilWarForts": true                                  // fortes LocTypeMilitaryFort (Guerra Civil)
  },
  "ai": { "skipCivilWarForts": true }                          // M16: a IA também não ataca esses fortes
}
```

Regras:
- **Nada de FormID de mod de terceiros sem conferir no xEdit.** Prefira `excludeFromPlugins` (só o nome do
  plugin) e marque `"verified": false` até alguém conferir.
- IDs sempre no formato curto `Plugin.esp|0xID` (ID local, sem o byte de load order), como no resto do mod.
- O DLL lê `Compat/*.json` em ordem alfabética, depois do `settings.json` e antes da descoberta. O log mostra
  `Compat: <nome> ativo por '<plugin>' (N exclusões)` ou `inativo (nenhum plugin de whenLoaded carregado)`.
- A IA (M16) respeita as exclusões de conquista (`ai.respectConquestExclusions`, padrão ligado).

## Patches incluídos

| Pasta | O que faz | Estado |
|---|---|---|
| `civil-war-mods/` | Não conquista locais de mods de guerra civil conhecidos; respeita fortes da Guerra Civil | Nomes de plugin a verificar (ADR 0042) |
