# 0066. Patch no CommonLibSSE-NG para detecção de runtime AE (versões do jogo ≥ 1.7.x)

- **Status:** Aceita
- **Data:** 2026-10-29
- **Origem:** Bug de usuário (crash no primeiro teste em jogo, antes de qualquer missão rodar)

## Contexto
Usuário reportou `AgeOfSkyrim.dll: REL/ID.h(219): failed to open address library file` ao abrir o jogo
(Skyrim SE/AE versão `1.7.104.0`), mesmo com o Address Library instalado e com o arquivo
`versionlib-1-7-104-0.bin` presente em `Data/SKSE/Plugins/`. Teste de isolamento (renomear `AgeOfSkyrim.dll`
para carregar por último, depois de todos os outros plugins SKSE) reproduziu o erro idêntico — descartando
ordem de carregamento/antivírus como causa.

Rastreada a causa até `extern/CommonLibSSE-NG/include/REL/Module.h`, `Module::load_version()`:
```cpp
switch (_version[1]) {           // minor version do executável
    case 4:  _runtime = Runtime::VR;  break;
    case 6:  _runtime = Runtime::AE;  break;
    default: _runtime = Runtime::SE;
}
```
Só os minors `4` (VR) e `6` (AE, i.e. `1.6.x`) são tratados explicitamente; qualquer outro minor — incluindo
`7` (`1.7.x`, inexistente quando o commit do CommonLibSSE-NG foi escrito) — cai no `default` e é classificado
como **SE pré-AE**. Isso faz `Module::IsAE()` retornar `false`, e o carregador do Address Library montar o
nome de arquivo errado: `version-1-7-104-0.bin` (sem "lib", formato SE antigo, que não existe) em vez de
`versionlib-1-7-104-0.bin` (formato AE, que de fato está instalado). Confirmado: não é bug nosso, nem do
Address Library do usuário — é uma lacuna do CommonLibSSE-NG (pin atual: `v3.7.0-129-gb93280e8`, já o commit
mais recente do `origin/main` do projeto — não há correção rio-acima ainda disponível para aplicar via bump
do submódulo).

**Confirmação externa:** existe a PR `CharmedBaryon/CommonLibSSE-NG#113` ("fix(rel): support Skyrim 1.7.x
runtime detection and address library fallback"), com descrição do problema idêntica (mesmo arquivo, mesma
linha, mesmo sintoma em `1.7.99`/`1.7.104+`) — fechada pelo próprio autor em 2026-10-12, sem revisão nem
merge. Valida o diagnóstico, mas não foi adotada tal qual: ela só adiciona `case 7:` (quebraria de novo em
`1.8.x`) e inclui um fallback especulativo e não revisado para um suposto formato `versionl2b-*.bin`
("Format 5") que não existe na instalação do usuário (só há arquivos `versionlib-*.bin`, o formato que o
`ID.h` já monta corretamente assim que `IsAE()` retornar certo). Por isso o patch adotado aqui só muda
`Module.h` (qualquer minor `>= 6` vira AE por `default`, não só o `7`) e não mexe em `ID.h`.

## Decisão
**Patch aplicado no CI, não no pin do submódulo.** `tools/vendor-patches/commonlibsse-ng-ae-version-detect.patch`
corrige o `switch`: minor `5` vira o único caso explícito de SE (foi a única minor version que a SE pré-AE
teve, `1.5.97`); qualquer minor `>= 6` (6, 7, e qualquer futura) vira AE por `default`. `.github/workflows/build.yml`
aplica esse patch com `git apply --directory=extern/CommonLibSSE-NG` logo após o checkout, antes de qualquer
outra etapa (`git apply --check` primeiro, pra falhar cedo e visível se o patch parar de bater quando o
submódulo for atualizado no futuro).

Alternativas descartadas:
- **Fazer fork do CommonLibSSE-NG e apontar o submódulo pra lá:** resolveria de forma mais "permanente", mas
  cria um repositório pra manter e sincronizar com o upstream indefinidamente, por uma correção de 3 linhas.
  Descartado por manutenção desproporcional ao problema (princípio de performance/compat não exige isso; é só
  simplicidade).
- **Editar o submódulo direto e comitar lá dentro:** quebraria builds reproduzíveis — um clone novo
  (`git submodule update --init`) busca o commit do pin no remoto real do CharmedBaryon/CommonLibSSE-NG, onde
  o nosso commit local nunca existiria.
- **Hotfix só no nosso código (sem tocar o vendor):** não existe hook público no CommonLibSSE-NG pra sobrescrever
  a detecção de runtime antes do `SKSE::Init()` rodar (a variável de ambiente `SKSE_RUNTIME` só troca o nome do
  executável procurado, não o tipo de runtime classificado).

## Consequências
- Quando o CommonLibSSE-NG upstream corrigir isso (ou simplesmente adicionar mais `case`s pros minors
  seguintes), o `git apply --check` vai falhar no CI assim que o submódulo for atualizado — aviso explícito pra
  remover o patch, não um bug silencioso reintroduzido.
- O patch é específico desse pin do submódulo (`b93280e8`); se o submódulo for atualizado por qualquer motivo
  antes do upstream corrigir isso, reconferir se a função/linhas ainda batem.
- Nenhum impacto em save/cosave/gameplay — é puramente sobre o DLL conseguir carregar.
