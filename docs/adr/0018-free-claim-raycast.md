# 0018. Free Claim: raycast até o chão e alinhamento à normal do terreno

- **Status:** Aceita
- **Data:** 2026-10-27
- **Origem:** Técnica

## Contexto
O Free Claim (M8) deixa o jogador fincar um estandarte em qualquer lugar ao ar livre. O objeto precisa ficar
no chão (não flutuando nem enterrado), acompanhar a inclinação do terreno e persistir no save, sem editar
células no ESP ([ADR 0007](0007-objetos-em-runtime.md)).

## Decisão
- **Raycast**: `bhkWorld::PickObject(bhkPickData&)` do mundo físico da célula do jogador, com um raio
  **vertical** que começa 200 unidades acima e termina 800 abaixo de um ponto `forwardDistance` à frente do jogador.
  Camada de colisão `kLOS` (terreno + estáticos). Coordenadas convertidas com `bhkWorld::GetWorldScale()`; ponto de
  impacto = `from + (to − from) × hitFraction`; normal = `rayOutput.normal` (invertida se apontar para baixo).
  Leitura sob `BSReadLockGuard(worldLock)`.
  - Descartados: `CrosshairPickData` (só vale com alvo na mira e não dá normal) e raio a partir da câmera
    (pega o próprio jogador/terceira pessoa e varia com o zoom).
- **Fallback**: se a célula não tiver `bhkWorld`, usa a altura do jogador e nenhuma inclinação (logado).
  Se o raio não acertar nada, o claim é recusado ("nenhum chão").
- **Alinhamento**: a normal é levada ao referencial do heading do jogador; `angle.x = atan2(n·frente, n.z)` e
  `angle.y = −atan2(n·direita, n.z)`, `angle.z = heading` (convenção Gamebryo). Inclinação acima de
  `maxSlopeDegrees` é recusada.
- **Criação**: `Placement::PlaceAt` (`PlaceObjectAtMe` persistente + `data.angle` + `SetPosition`) e
  `SetMotionType(kKeyframed)` para o objeto não cair nem tombar (retenta no próximo frame se o 3D ainda não existe).
  Objeto base configurável (`claim.banner`); padrão provisório Iron Sword (`Skyrim.esm|0x012EB7`) até existir
  um estandarte próprio no ESP.
- **Persistência**: registro `CLAM` v1 (serial, e por claim: Id estável `claim/<n>`, worldspace, posição, FormID do
  ref), com `ResolveFormID` na carga. Limites: `minDistance` entre claims no mesmo worldspace, `maxClaims`, sem
  interiores.

## Consequências
- O ângulo é uma aproximação (pequenas inclinações ficam corretas; o sinal é validado no teste em ladeira da M8).
- Um claim ainda **não** vira território: a ligação com facções/territórios fica para depois de M3/M4 (TODO em
  `World/Claims.h`).
