# 0055. "Dominar Skyrim" como vitória suave; lei e prisioneiros; ordem pública e rebeliões; troca de jarl

- Status: Aceita
- Data: 2026-10-28
- Tipo: Decisão do usuário

## Contexto
Perguntado sobre o comparativo com o Conquest of Skyrim (ADR 0030/0031, `docs/COMPARATIVO-CONQUEST-OF-SKYRIM.md`
seção 4), o usuário escolheu quais funcionalidades ainda fora do roadmap entram como missões novas, e como deve
ser a condição de vitória do mod. Decisão explícita de seguir implementando em paralelo à M16/M17, aceitando
risco de retrabalho no merge.

## Decisões
- **Vitória suave ("Dominar Skyrim"):** ao controlar todas (ou quase todas, configurável) as províncias, a
  facção do jogador recebe um título/marco especial ("Dominador(a) de Skyrim") e uma notificação única — **sem
  encerrar o mod**. O jogador continua jogando normalmente: defender invasões (M16), comércio (M13), rebeliões
  (nova), diplomacia (M17). Implementado como extensão do sistema de títulos (M22), não como quest com final.
  Diferente do Moot/coroação do CoS, que trava o jogo depois da vitória.
- **Sem diálogo com voz:** NPCs da facção (mordomo, comandante, jarls substitutos, prisioneiros) só têm texto
  (traduzido en/pt-br), sem dublagem sintética. Mantém o mod leve e sem depender de ferramentas de voz externas.
- **Novas missões (Fase 4, depois de M22):**
  - M24. Troca de jarl / governo local.
  - M25. Lei e prisioneiros de guerra.
  - M26. Ordem pública e rebeliões.
  - M27. Domínio de Skyrim (a vitória suave acima).
  - Cercos de capitais, pesquisa militar e diálogo com voz continuam fora do roadmap (não escolhidos).
- **Execução:** em paralelo à M16 (IA)/M17 (diplomacia), que ainda estão em andamento, por escolha do usuário.

## Consequências
- Risco de conflito de merge maior: M25/M26 podem depender de dados de combate/ataque que a M16 ainda está
  desenhando (POWs, origem do ataque); documentar suposições e expor pontos de integração, não bloquear.
- `Realm::Modifiers` ganha um modificador de estabilidade (aditivo); `Realm::Titles`/`Government` ganham os
  marcos de domínio e o conceito de jarl substituto, sem tocar a Guerra Civil vanilla (ADR 0005).
