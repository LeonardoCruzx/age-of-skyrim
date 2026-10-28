# Registros de decisão (ADRs)

Cada arquivo registra uma decisão: contexto, o que foi decidido e as consequências.
Decisões novas recebem o próximo número; uma decisão revista ganha um ADR novo e a antiga é marcada como *Substituída*.

| # | Decisão | Origem | Status |
|---|---|---|---|
| 0001 | [Um único DLL SKSE com CommonLibSSE-NG](0001-plugin-skse-commonlibsse-ng.md) | Técnica | Aceita |
| 0002 | [IDs estáveis por hash FNV-1a das chaves de texto](0002-ids-por-hash.md) | Técnica | Aceita |
| 0003 | [Estado do mod no cosave do SKSE, não em Papyrus](0003-estado-no-cosave.md) | Técnica | Aceita |
| 0004 | [Bandeiras por estilo (heráldica), não por facção](0004-estilos-de-bandeira.md) | Técnica | Aceita |
| 0005 | [Performance e integração com outros mods como prioridade](0005-principios-performance-compatibilidade.md) | Decisão do usuário | Aceita |
| 0006 | [Build pelo GitHub Actions (windows-2022 + vcpkg recente)](0006-build-no-github-actions.md) | Técnica | Aceita |
| 0007 | [Objetos do mod criados em runtime, sem editar células no ESP](0007-objetos-em-runtime.md) | Técnica | Aceita |
| 0008 | [Eu faço tudo; o usuário só testa, com roteiro curto e autoteste](0008-autoteste-e-fluxo-de-testes.md) | Decisão do usuário | Aceita |
| 0009 | [Conquista de território quando o local é limpo](0009-conquista-por-local-limpo.md) | Decisão do usuário | Aceita |
| 0010 | [Interface: MCM primeiro, PrismaUI depois, sobre a mesma API](0010-ui-mcm-depois-prismaui.md) | Decisão do usuário | Aceita |
| 0011 | [Sede da facção escolhida entre fortes/torres candidatos](0011-sede-entre-candidatas.md) | Decisão do usuário | Aceita |
| 0012 | [Estoque da sede com itens reais contados por categoria](0012-estoque-itens-reais.md) | Decisão do usuário | Aceita |
| 0013 | [Mesa de guerra na sede abre mapa tático em PrismaUI](0013-mesa-de-guerra-mapa-tatico.md) | Decisão do usuário | Aceita |
| 0014 | [Mod traduzido em inglês e português do Brasil](0014-traducao-en-ptbr.md) | Decisão do usuário | Aceita |
| 0015 | [Desenvolvimento em missões sequenciais](0015-roadmap-em-missoes.md) | Decisão do usuário | Aceita |
| 0016 | [Territórios descobertos automaticamente dos locais do jogo](0016-territorios-auto-descobertos.md) | Técnica | Aceita |
| 0017 | [Classificação do estoque por keyword: implementação](0017-estoque-classificacao-por-keyword.md) | Técnica | Aceita |
| 0018 | [Free Claim: raycast até o chão e alinhamento à normal do terreno](0018-free-claim-raycast.md) | Técnica | Aceita |
| 0019 | [Facções como donas de territórios](0019-modelo-de-faccoes.md) | Técnica | Aceita |
| 0020 | [Melhorias de territórios (custo + tempo de obra, visual por nível)](0020-melhorias-de-territorios.md) | Decisão do usuário | Aceita |
| 0021 | [Implementar as missões restantes sem esperar testes; teste único no fim](0021-implementar-tudo-teste-unico-no-fim.md) | Decisão do usuário | Aceita |
| 0022 | [Conquista por local limpo: implementação](0022-conquista-implementacao.md) | Técnica | Aceita |
| 0023 | [Sede da facção: candidatas, pergunta, objetos e cosave](0023-sede-implementacao.md) | Técnica | Aceita |
| 0024 | [Free Claim vira território dinâmico (e ordem do cosave)](0024-claims-como-territorios.md) | Técnica | Aceita |
| 0025 | [Objetos base do baú e da mesa de guerra escolhidos entre os vanilla](0025-objetos-base-da-sede.md) | Técnica | Aceita |
| 0026 | [Tick da economia por hora de jogo, disparado por eventos baratos](0026-tick-por-hora-orientado-a-evento.md) | Técnica | Aceita |
| 0027 | [Economia: produção data-driven, depósito da sede, saldos e consumo](0027-economia-producao-deposito-consumo.md) | Técnica | Aceita |
| 0028 | [Rotas de comércio: renda por distância/tipo e corte por território inimigo](0028-rotas-de-comercio.md) | Técnica | Aceita |
| 0030 | [Dogfish / Conquest of Skyrim como referência de design (sem copiar conteúdo)](0030-referencias-externas-dogfish.md) | Decisão do usuário | Aceita |
| 0031 | [Fase 4: governo e doutrina, população, exército, batalhas em tempo real, títulos](0031-exercito-governo-populacao-titulos.md) | Decisão do usuário | Aceita |
| 0032 | [API pública para outros mods: interface C++ versionada, eventos e natives Papyrus](0032-api-publica-para-outros-mods.md) | Técnica | Aceita |
| 0033 | [MCM do SkyUI como casca fina, com textos vindos do DLL](0033-mcm-skyui-casca-fina.md) | Técnica | Aceita |
| 0034 | [AgeOfSkyrim.esp gerado por script (sem Creation Kit)](0034-esp-gerado-sem-creation-kit.md) | Técnica | Aceita |
| 0035 | [Scripts Papyrus compilados no CI com Caprica e stubs próprios](0035-papyrus-compilado-no-ci-caprica.md) | Técnica | Aceita |
| 0036 | [Governo: doutrinas, cargos e governadores como dados + bônus numéricos](0036-governo-doutrina-cargos.md) | Técnica | Aceita |
| 0037 | [População numérica por território, crescimento logístico e cosave compacto](0037-populacao-numerica.md) | Técnica | Aceita |
| 0038 | [Títulos por marcos, avaliados só em eventos, que nunca rebaixam](0038-titulos-progressao.md) | Técnica | Aceita |
| 0039 | [Módulo `src/Realm`: modificadores agregados, ganchos de integração e relógio próprio](0039-reino-modulo-integracao.md) | Técnica | Aceita |
| 0051 | [PrismaUI opcional: interface mínima própria, view lazy e ponte JSON](0051-prismaui-opcional-interface-minima.md) | Técnica | Aceita |
| 0052 | [Mapa tático: um snapshot JSON por envio, só ao abrir e quando muda, e camadas por provedor](0052-snapshot-json-do-mapa-e-camadas.md) | Técnica | Aceita |
| 0053 | [Mapa tático sem PrismaUI: cadeia de MessageBox vanilla](0053-mapa-fallback-menus-vanilla.md) | Técnica | Aceita |
| 0054 | [Alvos de ataque marcados no mapa ficam no cosave (`WMAP` v1)](0054-alvos-de-ataque-no-cosave.md) | Técnica | Aceita |
| 0040 | [Release em tag `v*` reaproveitando o artefato do build do mesmo commit](0040-pipeline-de-release.md) | Técnica | Aceita |
| 0041 | [Instalador FOMOD: núcleo obrigatório + patches opcionais, idioma automático](0041-fomod-layout.md) | Técnica | Aceita |
| 0042 | [Patches de compatibilidade como dados (`Compat/*.json`) e lista de mods de guerra civil](0042-patches-de-compatibilidade.md) | Técnica | Aceita (loader pendente) |
