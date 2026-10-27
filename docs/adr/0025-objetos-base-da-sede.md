# 0025. Objetos base do baú e da mesa de guerra escolhidos entre os vanilla

- **Status:** Aceita
- **Data:** 2026-10-27
- **Origem:** Técnica (M6)

## Contexto
Sem ESP, baú e mesa de guerra precisam de objetos base vanilla. O baú **não pode ter respawn** (o conteúdo seria
resetado com a célula) e não deve trazer itens próprios; a mesa precisa ser **ativável** para a M11 abrir o mapa.
Eu não tenho FormIDs de containers/ativadores vanilla com confiança, e um FormID errado daria um baú com respawn.

## Decisão
- `settings.json → seat.chest` / `seat.warTable`: `"auto"` (padrão) ou `"Plugin.esp|0xID"`.
- **Baú automático**: primeiro `TESObjectCONT` do `Skyrim.esm` (ordem do arquivo, determinística) **sem** o flag
  `kRespawn`, sem itens no objeto base, com nome e modelo contendo `chest` — preferindo os que contêm `noble`.
  Um `seat.chest` configurado com respawn é recusado (aviso) e cai na escolha automática.
- **Mesa automática**: primeiro `TESObjectACTI` do `Skyrim.esm` com nome (ativável) e modelo contendo `wartable`,
  depois `maptable`, depois `table`. Sem nenhum, um `TESObjectSTAT` com `wartable`/`maptable` (só visual, sem
  ativação — aviso no log). Sem nada, a sede funciona sem mesa.
- O log registra o escolhido: `Plugin|0xID`, nome, tipo e caminho do modelo — para conferir no teste e fixar depois.
- Quando o `AgeOfSkyrim.esp` (ESL) tiver baú e mesa próprios, basta apontar `seat.chest`/`seat.warTable` para eles.

## Consequências
- Nada é editado nos registros vanilla; outros mods que adicionam containers não interferem (só `Skyrim.esm`).
- A escolha pode cair num objeto com aparência estranha ou, no caso do ativador, com script próprio; o teste confere
  e, se preciso, fixamos o FormID no `settings.json`.
