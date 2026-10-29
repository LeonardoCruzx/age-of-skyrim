# 0066. Patch no CommonLibSSE-NG para Skyrim ≥ 1.7.x: detecção de runtime AE e formato do address library

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
`1.8.x`) e inclui um fallback para um suposto formato `versionl2b-*.bin` ("Format 5") que, na primeira
rodada desta ADR, avaliei como especulativo e não copiei por não haver esse arquivo confirmado na instalação
do usuário.

**Segunda camada do problema (achada só depois, com o DLL já carregando):** corrigido o `Module.h`, o jogo
chegou a abrir, mas travou de novo com `REL/ID.h(257): Unsupported address library format: 5` — o Address
Library do usuário (`Address Library All in One` v13) já gera `versionlib-1-7-104-0.bin` num **formato 5**
novo, que `IDDatabase::header_t::read()` não reconhece (só lê formato `2`, hardcoded em `IDDatabase::load()`
via `Module::IsAE() ? 2 : 1`). A parte da PR #113 que eu tinha descartado por "especulativa" era exatamente
sobre isso — a mudança de formato é real, só a composição do nome do arquivo que a PR chutou
(`versionl2b-*.bin`) não pôde ser confirmada no caso do usuário. Em vez de pedir mais uma rodada de
verificação manual, o segundo patch
(`tools/vendor-patches/commonlibsse-ng-ae-address-library-format-fallback.patch`) faz o próprio código
decidir em runtime: se existir `versionl2b-{versão}.bin` (companheiro legado em formato `2`, que esta cópia
já sabe ler) ao lado do `versionlib-{versão}.bin`, usa o legado; senão, mantém o comportamento atual. Sem
suporte a ler o formato `5` de verdade — não há documentação pública do layout binário novo para
implementar isso com segurança (um parser errado leria offsets/ids corrompidos silenciosamente, pior que
travar com uma mensagem clara).

## Decisão
**Dois patches aplicados no CI, não no pin do submódulo.** `.github/workflows/build.yml` aplica ambos com
`git apply --directory=extern/CommonLibSSE-NG` logo após o checkout, antes de qualquer outra etapa
(`git apply --check` primeiro em cada um, pra falhar cedo e visível se algum parar de bater quando o
submódulo for atualizado no futuro):
1. `tools/vendor-patches/commonlibsse-ng-ae-version-detect.patch` — corrige o `switch` de `Module.h`: minor
   `5` vira o único caso explícito de SE (foi a única minor version que a SE pré-AE teve, `1.5.97`); qualquer
   minor `>= 6` (6, 7, e qualquer futura) vira AE por `default`.
2. `tools/vendor-patches/commonlibsse-ng-ae-address-library-format-fallback.patch` — em `ID.h`, antes de
   montar o nome do arquivo AE, checa se `versionl2b-{versão}.bin` existe; se sim, usa ele (formato `2`,
   legado, lido normalmente); senão, mantém `versionlib-{versão}.bin` como antes.

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
- Quando o CommonLibSSE-NG upstream corrigir isso (ou simplesmente adicionar mais `case`s/formatos), o
  `git apply --check` vai falhar no CI assim que o submódulo for atualizado — aviso explícito pra remover o(s)
  patch(es), não um bug silencioso reintroduzido.
- Os patches são específicos desse pin do submódulo (`b93280e8`); se o submódulo for atualizado por qualquer
  motivo antes do upstream corrigir isso, reconferir se as funções/linhas ainda batem.
- **Se o usuário não tiver `versionl2b-*.bin`** (Address Library mais antigo, ou uma versão do jogo onde esse
  companheiro legado não é gerado), o patch 2 não muda nada e o erro de formato 5 continua — nesse caso não
  há solução no nosso lado até o CommonLibSSE-NG ganhar um parser de verdade pro formato novo, ou o usuário
  trocar de versão do Address Library/jogo.
- Nenhum impacto em save/cosave/gameplay — é puramente sobre o DLL conseguir carregar.
