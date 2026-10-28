# Roteiros de teste

No fim de cada teste, envie os arquivos da pasta `Documentos\My Games\Skyrim Special Edition\SKSE\`:
`AgeOfSkyrim.log` e `AgeOfSkyrim_territories.json`.

## Instalação (uma vez por versão)

1. No GitHub: **Actions → Build →** o run mais recente da `main` com ✅ **→ Artifacts →** baixe `AgeOfSkyrim-...`.
2. No MO2: *Install a new mod from an archive* → escolha o zip (no Vortex: arraste o zip).
   Ative o mod. Requisitos: SKSE64 e Address Library for SKSE Plugins.
3. Abra o jogo **pelo SKSE** (no MO2, o executável `SKSE`).

---

## Teste atual — M2, M3, M4, M7 e M8 juntas (≈ 10 min)

Não precisa de ESP. O mod cria 3 objetos de teste na sua frente, um por facção:
**moeda de ouro** = Legião Imperial · **lingote de ferro** = Mantos da Tempestade · **sweetroll** = sua facção de teste.
Não pegue nenhum objeto criado pelo mod.

1. **Carregue um save** e fique num lugar **aberto** (ex.: na estrada, fora de Whiterun).
   Tenha no inventário um pouco de ouro, comida e algum lingote/minério.
2. Aperte **F11** (autoteste). Em cima, à esquerda, deve aparecer **"AoS autoteste OK (...)"** e fica visível
   **só o sweetroll**. Depois aparece **"AoS teste de texto: ação, coração, você, à"** — repare nos acentos.
   - Aperte **F10** algumas vezes: o objeto visível troca (nada → moeda → lingote → sweetroll) e aparece
     "AoS: Torre de teste -> ...". Termine apertando **F11** de novo (volta ao sweetroll).
3. Aperte **F8** (estoque): aparece uma linha por categoria do seu inventário ("Ouro: ...", "Comida: ...", ...).
   Confira se o ouro bate com o do inventário.
4. Aperte **F3** (fincar estandarte) olhando para o chão à frente: surge uma **espada de ferro** parada no chão.
   Aperte **F3** de novo no mesmo lugar → **"muito perto de outro claim"**.
   Ande uns 30 m até uma **ladeira** e aperte **F3**: repare se a espada **inclina acompanhando o chão**
   (para o lado certo ou ao contrário).
5. **Salve** (save novo), **carregue esse save** → deve aparecer **"AoS persistência OK. Teste concluído!"**
   e as espadas continuam lá. Feche o jogo e me envie os 2 arquivos + diga:
   **os acentos do passo 2 apareceram certos?** e **para que lado a espada inclinou no passo 4?**

**Se algo der diferente** (mensagem de FALHOU, nenhuma mensagem, objeto errado, crash): só me envie os
arquivos e diga em qual passo parou.

O que cada passo verifica (para mim): 2 = bandeiras por dono (M2) + facções (M3) + acentos;
3 = estoque por categoria (M7); 4 = Free Claim (M8); 5 = cosave de tudo. A descoberta de territórios (M4)
roda sozinha ao abrir o jogo e vai para o log + `AgeOfSkyrim_territories.json`.
