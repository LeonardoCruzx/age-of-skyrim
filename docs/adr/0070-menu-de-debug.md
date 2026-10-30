# 0070. Menu de debug por nome, além das hotkeys (M30)

- **Status:** Aceita
- **Data:** 2026-10-30
- **Origem:** Pedido do usuário (teclado 70%, sem teclado numérico; propôs um estandarte "interativo" pra
  ajudar no debug além de decorar tecla por tecla — redirecionado para um menu por hotkey única)

## Contexto
O roteiro de teste (`TESTES.md`) acumulou 24 ações de debug, cada uma com sua própria tecla (ADR da troca
pra fora do NumPad, feita ainda nesta sessão). Usuário pediu alguma forma de acessar isso sem precisar
decorar/alcançar tecla por tecla. Avaliadas três formas de disparar isso (pergunta ao usuário): ativar o
estandarte do Free Claim, ativar a mesa de guerra da sede, ou uma hotkey única abrindo uma lista — escolhida
a hotkey única, por funcionar em qualquer lugar (não depende de ter plantado estandarte ou fundado sede).

Pra tecla: usuário pediu F4. F4 já era `conquestHotkey` ("simular conquista", passo D do roteiro) — colocar
o menu ali em cima criaria duas ações disparando pela mesma tecla ao mesmo tempo. Perguntado como resolver:
decisão do usuário foi deixar o menu em F4 e tirar a tecla dedicada da conquista — ela continua existindo,
só que agora só pelo menu (categoria D).

## Decisão
1. **`Debug/DebugActions.{h,cpp}` (novo):** as 24 ações (antes inline em `Events.cpp`, uma por `if` no sink
   de input) viram funções livres com nome, cada uma só chamando a função real do sistema correspondente
   (`Economy::Get().DebugForceHours(...)`, `Realm::ShowSummary()`, etc.) — nenhuma lógica de jogo nova, só um
   nome estável por ação. `Events.cpp` passa a ter uma tabela `{tecla, função}` no lugar do `if`-chain
   antigo (também usada pelos hotkeys M18/M19/M22/M24, que já viviam em sinks próprios em `Realm.cpp`/
   `Jarl.cpp` — esses continuam como estavam, só a chamada interna agora passa por `DebugActions`).
2. **`UI/DebugMenu.{h,cpp}` (novo):** duas camadas de `MessageBoxData` vanilla (`UI/Prompt`, mesmo padrão já
   usado em `UI/WarMapMenus.cpp` pro mapa tático sem PrismaUI) — categorias A-M (o mesmo agrupamento de
   `TESTES.md`) → ações da categoria → roda a função de `DebugActions` e volta pras categorias. Paginado
   (6 por página, igual ao mapa tático) porque 13 categorias não cabem num `MessageBoxData` só.
3. `debug.menuHotkey` (novo, `settings.json`) = F4; `debug.conquestHotkey` removido (tecla dedicada tirada;
   `DebugActions::SimulateConquest()` só é chamada pelo menu agora).
4. Zero chave nova pra "voltar"/"continuar"/"fechar"/"página X de Y" — reaproveitadas as de `warmap.menu.*`
   (texto já genérico, não fala de território).

Efeito colateral descoberto no caminho (corrigido junto): os valores **padrão** em C++ (`Config.h`/`.cpp`,
usados só se uma chave faltar no `settings.json`) ainda apontavam pro NumPad antigo — a troca de teclas da
sessão anterior só tinha mudado o `settings.json` em si. Corrigido pra bater com o `settings.json` atual
(ver também `Config.cpp`: vários `dbg.value("chave", 0x..)` trocados por `dbg.value("chave", debug_.campo)`,
que já é o padrão usado pela maioria das linhas — alguns tinham ficado com o literal antigo hardcoded).

Alternativa descartada: ativar o estandarte do Free Claim ou a mesa de guerra pra abrir o menu — descartadas
pelo próprio usuário (ver Contexto) por só funcionarem onde algo já foi plantado/fundado.

## Consequências
- Nenhuma tecla muda de comportamento, exceto F4 (antes conquista, agora o menu) — todas as outras hotkeys
  do roteiro continuam idênticas, acessíveis pelas mesmas teclas E pelo menu.
- `TESTES.md` atualizado: tabela de teclas, nota sobre o menu, passo D explica o caminho pelo menu.
- Sem registro de cosave novo; é só UI/organização de código, nenhuma mudança de estado salvo.
