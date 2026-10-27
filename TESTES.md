# Roteiros de teste

Cada missão que precisa do jogo tem um roteiro curto aqui. No fim, envie o arquivo de log:
`Documentos\My Games\Skyrim Special Edition\SKSE\AgeOfSkyrim.log`

## Instalação (uma vez por versão)

1. No GitHub: **Actions → Build →** o run mais recente com ✅ **→ Artifacts →** baixe `AgeOfSkyrim-...`.
2. No MO2: *Install a new mod from an archive* → escolha o zip (no Vortex: arraste o zip).
   Ative o mod. Requisitos: SKSE64 e Address Library for SKSE Plugins.
3. Abra o jogo **pelo SKSE** (no MO2, o executável `SKSE`).

---

## M2 — Bandeiras + salvar/carregar (≈ 5 min)

Não precisa de ESP: o mod cria 3 objetos de teste na sua frente (moeda de ouro, lingote de ferro, sweetroll).

1. Carregue qualquer save e fique num lugar **aberto e plano** (ex.: na estrada, fora de Whiterun).
2. Aperte **F11**. Em cima, à esquerda, deve aparecer: **"AoS autoteste OK (...). Agora salve e carregue."**
   Na sua frente fica visível **só o lingote de ferro**. (Não pegue os objetos.)
   Logo depois aparece **"AoS teste de texto: ação, coração, você, à"** — repare se os acentos aparecem certos.
3. Aperte **F10** algumas vezes: a cada toque, o objeto visível muda
   (sweetroll → nada → moeda → lingote → ...) e aparece "AoS: Torre de teste -> ...".
4. Aperte **F11** de novo (para voltar ao lingote), depois **salve** (save novo) e **carregue esse save**.
   Deve aparecer: **"AoS persistência OK. Teste concluído!"**
5. Feche o jogo e me envie o `AgeOfSkyrim.log` + diga se os **acentos** do passo 2 apareceram certos.

**Se algo der diferente** (mensagem de FALHOU, nenhuma mensagem, objeto errado, crash): só me envie o log
e diga em qual passo parou.
