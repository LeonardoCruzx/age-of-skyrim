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
| 0026 | [Tick da economia por hora de jogo, disparado por eventos baratos](0026-tick-por-hora-orientado-a-evento.md) | Técnica | Aceita |
| 0027 | [Economia: produção data-driven, depósito da sede, saldos e consumo](0027-economia-producao-deposito-consumo.md) | Técnica | Aceita |
| 0028 | [Rotas de comércio: renda por distância/tipo e corte por território inimigo](0028-rotas-de-comercio.md) | Técnica | Aceita |
