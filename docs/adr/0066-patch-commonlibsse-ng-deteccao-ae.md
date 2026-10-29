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
via `Module::IsAE() ? 2 : 1`). A parte da PR #113 sobre isso tinha um fallback especulativo pra um arquivo
`versionl2b-*.bin` que **não existe** na instalação do usuário (confirmado testando: o jogo continuou travando
mesmo com `AgeOfSkyrim` desativado no Vortex, até percebermos que era um `.dll` renomeado esquecido de um
teste anterior — depois de remover esse arquivo fantasma e reativar o mod, o erro de formato 5 continuou,
isolado e repetível, confirmando que não tem workaround por nome de arquivo).

**Solução real, não mais especulação:** com o jogo abrindo normalmente usando só PrismaUI/SkyUI/SKSE64/
Address Library (sem o nosso `.esp`), ficou provado que **dá pra ler esse formato** — só a nossa cópia do
CommonLibSSE-NG que não sabia. Rastreado até `PrismaUI-SKSE/framework`, que depende de "CommonLib-NG 4.2.0";
isso levou a `alandtse/CommonLibSSE-NG` (fork ativo, atualizado no mesmo dia desta ADR, até `v15.x`) — um
fork muito mais vivo que o `CharmedBaryon/CommonLibSSE-NG` que temos fixado (parado, sem commits novos desde
o nosso pin). O fork do alandtse já implementa o formato 5 de verdade
(`include/REL/IDDB.h`/`src/REL/IDDB.cpp`, classe `header_v5_t` + `IDDB::load_v5`): cabeçalho com nome do
módulo num campo **fixo de 64 bytes** (sem prefixo de tamanho, diferente do formato 1/2) seguido de um
**array denso** de offsets indexado direto pelo id (`0` = sem mapeamento) — nada da codificação delta do
formato antigo. Essa implementação bateu byte a byte com o `versionlib-1-7-104-0.bin` real do usuário,
decodificado à mão antes de escrever qualquer código (`format=5`, `version=[1,7,104,0]`,
`name="SkyrimSE.exe"`, depois `pointerSize`/`dataFormat`/`offsetCount` como `int32` cada, depois o array).

O patch adotado (`tools/vendor-patches/commonlibsse-ng-address-library-format5.patch`) **adapta** essa lógica
pra nossa estrutura em vez de copiar o arquivo do alandtse inteiro: nosso `IDDatabase::load_file` passa a
espiar o campo de formato antes de decidir; se for `5`, lê o cabeçalho novo e converte o array denso pra
nossa representação esparsa já existente (`{id, offset}` ordenado, usada pelo formato 1/2), reaproveitando
`id2offset()`/`Offset2ID` sem duplicar um segundo caminho de busca. Não copiamos o memory-mapping direto do
arquivo que o fork deles usa (otimização de desempenho, não essencial) — mantemos o mesmo mecanismo de
memória compartilhada nomeada (`CommonLibSSEOffsetsV5-<versão>`) que o formato 1/2 já usa aqui.

## Decisão
**Dois patches aplicados no CI, não no pin do submódulo.** `.github/workflows/build.yml` aplica ambos com
`git apply --directory=extern/CommonLibSSE-NG` logo após o checkout, antes de qualquer outra etapa
(`git apply --check` primeiro em cada um, pra falhar cedo e visível se algum parar de bater quando o
submódulo for atualizado no futuro):
1. `tools/vendor-patches/commonlibsse-ng-ae-version-detect.patch` — corrige o `switch` de `Module.h`: minor
   `5` vira o único caso explícito de SE (foi a única minor version que a SE pré-AE teve, `1.5.97`); qualquer
   minor `>= 6` (6, 7, e qualquer futura) vira AE por `default`.
2. `tools/vendor-patches/commonlibsse-ng-address-library-format5.patch` — adiciona leitura real do formato 5
   em `ID.h`/`ID.cpp` (`header_v5_t` + `IDDatabase::load_v5`). `load_file` passa a espiar o campo de formato
   e despachar pro caminho novo quando for `5`, em vez de assumir sempre o formato que o chamador pediu.
   **Nota de licença:** `alandtse/CommonLibSSE-NG` (onde o formato 5 foi localizado, ver Contexto) é
   GPL-3.0 — diferente da MIT do `CharmedBaryon/CommonLibSSE-NG` que temos fixado. Copiar o código-fonte
   deles poderia obrigar o DLL inteiro a ser redistribuído sob GPL, decisão que não cabe tomar sozinho numa
   correção técnica. O que foi usado de lá é só o **layout dos campos** (quantos bytes, em que ordem) — fato
   sobre o formato do arquivo, não expressão protegida por direito autoral; o mesmo layout já tinha sido
   decodificado à mão a partir do arquivo real do usuário antes mesmo de olhar o código deles (bateu). A
   implementação aqui usa estratégia diferente da deles: leitura sequencial convertendo pra lista esparsa
   reaproveitando `id2offset()`/`Offset2ID` já existentes, em vez do memory-map direto do arquivo com um
   caminho de busca denso separado que o fork deles usa — não é uma cópia do código-fonte GPL.

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
- Não copiamos o memory-mapping direto do arquivo (`create_from_file`) que o fork do alandtse usa pro
  formato 5 — convertemos pra lista esparsa em memória compartilhada nomeada, igual ao formato 1/2. Custo
  aceito: um pouco mais de memória/tempo de conversão no carregamento (evento único, não um caminho quente;
  ADR 0005), e um comentário deixado no patch caso algum dia valha a pena portar a versão com mmap direto.
- Nenhum impacto em save/cosave/gameplay — é puramente sobre o DLL conseguir carregar.
- Credito: layout do formato 5 adaptado de `alandtse/CommonLibSSE-NG` (fork open source, ativo), não
  inventado do zero — ver seção de Contexto para a atribuição completa.
