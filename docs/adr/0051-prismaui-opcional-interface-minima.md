# 0051. PrismaUI opcional: interface mínima própria, view lazy e ponte JSON

- **Status:** Aceita
- **Data:** 2026-10-28
- **Origem:** Técnica (M11, detalha a [ADR 0010](0010-ui-mcm-depois-prismaui.md) e a [ADR 0013](0013-mesa-de-guerra-mapa-tatico.md))

## Contexto
O mapa tático usa o PrismaUI (framework de UI web para SKSE), que é dependência **opcional**. Não queremos
nenhuma lib extra no vcpkg nem ligar o DLL ao `PrismaUI.dll`. O header público `PrismaUI_API.h` (repositório
`PrismaUI-SKSE/example-skse-plugin`) diz que pode ser copiado para outros projetos, mas não traz um arquivo de
licença formal, e o ambiente de desenvolvimento não conseguiu baixá-lo literalmente para revisão.

## Decisão
- **Interface mínima própria** em `src/UI/PrismaApi.h`, escrita a partir da documentação pública
  (prismaui.dev/api/*) e da lista de métodos do header: `IVPrismaUI1` com os 21 métodos virtuais **na mesma
  ordem** (a ordem define a vtable), os tipos dos callbacks e `Request()`, que faz
  `GetModuleHandleW(L"PrismaUI.dll")` + `GetProcAddress("RequestPluginAPI")` com `InterfaceVersion::V1`.
  Usamos só a V1 (a V2 só acrescenta o callback do console). Sem PrismaUI: `nullptr` → menus vanilla (ADR 0053).
- **Detecção em `kDataLoaded`** (o SKSE já carregou todos os plugins), em `WarMap::Load`.
  `settings.json → warMap.usePrismaUI: false` força os menus vanilla.
- **View lazy**: criada na primeira abertura da mesa (`CreateView("AgeOfSkyrim/index.html")`, relativo a
  `Data/PrismaUI/views/`); se o arquivo não existe, aviso no log e menus vanilla. Fechar = `Unfocus` + `Hide`
  (reabrir não recarrega o HTML). `Focus(view, pauseGame)` pausa o jogo por padrão (`warMap.pauseGame`).
- **Ponte**: C++ → JS por `InteropCall` (`aosWarMapData(snapshot)`, `aosWarMapResult({ok,text})`);
  JS → C++ por um único listener `aosWarMapAction(JSON)` com `action` = `createRoute` | `removeRoute` |
  `target` | `close` | `ready`. A doc não diz em que thread o callback roda, então ele só copia a string e
  agenda a execução com `SKSE::GetTaskInterface()->AddTask`. Snapshot que chega antes do DOM pronto fica guardado
  e é enviado no `onDomReady`.
- **Esc** é tratado na view (cancela a escolha da 2ª ponta da rota ou fecha).
- **View própria** em `data/PrismaUI/views/AgeOfSkyrim/` (HTML/CSS/JS sem dependências): mapa desenhado em SVG a
  partir das posições dos territórios (sem imagem de mapa), ícone por tipo, cor da facção, fecho convexo por
  província, linhas de vizinhança e rotas (ativa / cortada / inativa), painel do território, zoom/arrasto.
  Fora do jogo: `index.html?sample` carrega `sample/sample-data.js` (gerado por `tools/warmap_sample.py`) e simula
  as ações.

## Consequências
- Nenhum binário ou header de terceiros no repositório; se o PrismaUI mudar a V1 (não deveria: versões novas viram
  V2, V3…), só `PrismaApi.h` muda. Ponto a verificar no jogo: chamadas da API na thread principal e o Esc.
- Sem PrismaUI o mod funciona igual, só com menus mais simples.
