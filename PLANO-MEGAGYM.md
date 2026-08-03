# Megagym (YPOO `YPBM001264`) no QZ — caracterização, calibração e plano

> **Substitui `PLANO-BIKE-MEGAGYM.md`.** Aquele documento partiu da premissa de que
> a tarefa era escrever um driver, corrigiu-se para "nada a implementar", e as duas
> conclusões estavam incompletas: a detecção de fato já funciona, mas há **calibração
> real a fazer e dois defeitos de código** que ele não enxergou — porque foi escrito
> a partir de uma captura BLE de 113 s, sem nenhuma sessão real contra um app.
>
> Este documento é escrito a partir de **duas sessões reais no MyWhoosh** (2026-08-02)
> cruzadas com o log de debug do QZ e os `.fit` exportados pelo app.

**Convenção:** ✅ medido · ⚙️ verificado no código · ❓ desconhecido.

---

## 1. TL;DR

1. **O caminho de dados QZ→MyWhoosh é exato.** Potência e cadência passam 1:1. Não há erro de escala em lugar nenhum. ✅
2. **A bike não tem medidor de potência.** O watt é uma tabela de firmware `P = f(resistência, cadência)`. Calibrar = escolher o mapeamento; não há verdade a convergir sem referência externa. ✅
3. **A faixa útil é R 13–32, não 1–32.** Entre R=1 e R=13 a potência só vai de 80 a 111 W a 80 rpm — 12 níveis mortos. ✅
4. **As marchas do QZ são aplicadas duas vezes**, totalizando 6 níveis por marcha. Com `gears_from_bike`, 4 toques de paddle consomem a faixa inteira. ⚙️✅
5. **As marchas virtuais do MyWhoosh absorvem o terreno antes do FTMS.** O que chega ao QZ é demanda de resistência pós-marcha, não inclinação. ✅
6. **O ERG exige a setting `zwift_erg` ligada** — sem ela os pacotes de simulação sobrescrevem a resistência calculada pelo ERG. ⚙️ + confirmado pelo mantenedor.
7. **O keep-alive do ERG está furado** para apps que mandam simulação continuamente, como o MyWhoosh. ⚙️✅

---

## 2. O equipamento

**MAC** `B8:F8:62:6D:A8:D6` · **Nome BLE** `YPBM001264` (10 chars)

### 2.1 Detecção pelo QZ — correta, nada a fazer ⚙️

Casa com `startsWith("YPBM") && length() == 10` em `ftmsbike.cpp:2171` e
`bluetooth.cpp:2017`. Perfil atribuído, todos conferidos contra o hardware:

| Dado | Hardware | QZ | |
|---|---|---|---|
| `max_resistance` | 32 (`0x2AD6`: min 1, máx 32, passo 1) | 32 | ✅ |
| ERG | não (`0x2ACC` target bit 3 = 0) | `ergModeSupported = false` | ✅ |
| Modo | resistência-alvo (bit 2 = 1) | `resistance_lvl_mode = true` | ✅ |
| Codificação da escrita | nível × 10, little-endian | ramo ×10 de `ftmsbike.cpp:374` | ✅ |

`0x2ACC` bruto: `86-52-00-00-04-00-00-00` → Machine Features `0x5286`
(cadência, distância, resistência, energia, tempo, potência); Target Setting
Features `0x00000004` (só resistência-alvo).

A bike alterna dois pacotes `0x2AD2`, ~1 s cada: flags `0x01F5` (cadência,
distância, resistência, potência, potência média, energia) e `0x2A00`
(velocidade, FC, tempo decorrido/restante). O QZ decodifica os dois corretamente.

Característica proprietária YPOO `d18d2c10-c44c-11e8-a355-529269fb1459`, dentro do
serviço FTMS. Já conhecida do QZ (`horizontreadmill.cpp:2360`). Não é usada.

### 2.2 A potência é uma tabela de firmware, não uma medição ✅

**Este é o fato mais importante do documento e o plano antigo não o conhecia.**

Em 99 de 99 combinações `(resistência, cadência)` observadas 3+ vezes na sessão 1,
o watt reportado foi **exatamente o mesmo número**, com dispersão **0,00 W**.
Nenhum extensômetro tem ruído zero.

Consequências:
- O watt que chega ao MyWhoosh (e ao Strava, e ao seu histórico) é uma função
  determinística da resistência que o QZ manda.
- "Calibrar" significa escolher o mapeamento terreno→resistência que dá o esforço
  desejado. **Não há como validar o valor absoluto sem um medidor de potência
  externo (pedal/pedivela).** ❓
- A `ergTable` do QZ, que aprende essa mesma relação, está aprendendo uma tabela a
  partir de outra tabela — o que funciona, mas herda o viés do firmware.

### 2.3 A curva medida ✅

Da sessão 1 (cadência ≥ 55 rpm), watt por rpm em cada nível:

| R | n | W/rpm | W a 80 rpm | R | n | W/rpm | W a 80 rpm |
|---|---|---|---|---|---|---|---|
| 1 | 24 | 0,997 | 80 | 19 | 53 | 1,921 | 154 |
| 6 | 6 | 1,235 | 99 | 20 | 31 | 2,022 | 162 |
| 7 | 5 | 1,297 | 104 | 21 | 35 | 2,078 | 166 |
| 8 | 7 | 1,350 | 108 | 22 | 66 | 2,188 | 175 |
| 13 | 19 | 1,385 | 111 | 23 | 48 | 2,305 | 184 |
| 14 | 4 | 1,600 | 128 | 24 | 29 | 2,425 | 194 |
| 15 | 10 | 1,658 | 133 | 25 | 20 | 2,561 | 205 |
| 16 | 434 | 1,696 | 136 | 26 | 19 | 2,677 | 214 |
| 17 | 109 | 1,749 | 140 | 27 | 11 | 2,816 | 225 |
| 18 | 61 | 1,854 | 148 | 32 | 10 | 3,948 | 316 |

**Zona morta R 1–13:** 12 níveis valem 31 W. **Faixa útil: R 13–32** (111 → 316 W).

Um ajuste linear `W = (0,0728 + 0,0987·R)·rpm` reproduz R 13–27 dentro de ±3%, mas
erra +481% em R=1 e −22% em R=32. **Não usar fora de 13–27.**

> ⚠️ As pontas têm poucas amostras (R=6..8: 5–7 cada; R=32: 10) e R 28–31 não foi
> observado. A Fase 2 do plano existe para fechar isso.

---

## 3. Como o QZ traduz terreno em resistência

### 3.1 A fórmula ⚙️✅

`CharacteristicWriteProcessor::changeSlope()` — `characteristicwriteprocessor.cpp`:

```
R = round(1,5 × grade% × bike_resistance_gain_f) + bike_resistance_offset + 1
    + CRR_offset + CW_offset
```

Com `CRRGain` e `CWGain` nos defaults (**0**), os dois últimos termos somem.
Verificado numericamente contra o log: grade 1,18% → 15; −0,68% → 12; −1,82% → 10.

Semântica do offset, nas palavras do mantenedor ([#3230]): *"N on resistance offset
means that at 0% grade you have resistance N on the bike"* — na prática `offset + 1`.

### 3.2 Defeito: marchas aplicadas em dobro ⚙️✅

| Onde | Peso |
|---|---|
| `bike.cpp:73` — `RequestedResistance = resistance × m_difficult + gearsModifier()` | +1× |
| `ftmsbike.cpp:509` — `rR = requestResistance + (gearsModifier() × 5)` | +5× |

**Total: 6 níveis por marcha.** Traço do log às 12:00:24, com marcha +4:

```
grade 0,97%  →  changeResistance 14  →  "writing resistance 18" (+4)
             →  forceResistance 38 (+20)  →  "38 exceeds max_resistance 32 - clamping"
```

Com `gears_from_bike = true`, **cada toque de paddle vira uma marcha**. Na sessão 1
foram 4 toques em 4 s → +24 níveis → por 19 s a bike ficou travada em 32 e todo o
relevo de −2,01% a +11,88% virou o mesmo valor. Distribuição de marcha na sessão 1:
**+1 em 86% do tempo** (= +6 níveis permanentes), +4 em 1,8%, −2 em 8,7%.

> O mantenedor observa em [#3300] que **a marcha precisa estar em 0 para o ERG
> funcionar**, e que gostaria de reverter esse acoplamento.

### 3.3 Knob de dificuldade — o substituto correto do paddle ⚙️

O `+`/`−` do tile **target resistance** (`homeform.cpp:5317`) mexe em `difficult`,
que é **multiplicativo** (`resistência × difficult`), em passos de 3%, e **não sofre
a dupla contagem**. Não é persistido: volta a 100% a cada sessão
(`bluetoothdevice.h:649`).

---

## 4. Como o MyWhoosh se comporta

### 4.1 As marchas virtuais absorvem o terreno ✅

Reconstruindo a inclinação real pela altimetria do `.fit` e comparando com os 600
pacotes `0x11` recebidos:

| | média | desvio | mín | máx |
|---|---|---|---|---|
| Terreno (altimetria) | 0,88% | 3,60 | −7,01% | +14,54% |
| Grade enviada por BLE | 0,02% | 2,69 | −2,01% | +11,88% |

**r = 0,130**, inclinação 0,097. Varredura de defasagem de −40 a +40 s em quatro
janelas de suavização: melhor caso **r = 0,31**. Não é desalinhamento.

Trecho de 1 Hz numa rampa real:

```
hora     terreno%   BLE%   pot W   vel km/h
15:10:35   +5,55    -2,01    149      24,5
15:10:41   +6,92    -1,10    152      12,0
15:10:44   +4,46    -1,85    155       9,8
15:11:08   -6,37    -2,01    155      34,1
```

A velocidade no jogo responde ao relevo (30 → 9,8 → 41 km/h), logo a **simulação
usa o terreno**. Mas o que é pedido à bike fica colado no piso, e a potência não sai
de ~150 W. Fechando: `corr(potência, grade BLE) = +0,414` contra
`corr(potência, terreno) = −0,045`.

**A grade que chega ao QZ é demanda de resistência pós-marcha.** Qualquer análise que
a trate como inclinação está errada.

### 4.2 Distribuição real da demanda ✅ (n=600, sessão 1)

| mín | p5 | p25 | p50 | p75 | p95 | máx |
|---|---|---|---|---|---|---|
| −2,01 | −2,01 | −0,77 | +1,24 | +3,06 | +7,89 | +11,88 |

5,5% no piso de −2,01%; 10% acima de +5%. CRR = 53 (0,0053) e CW = 51, constantes.

**Cadência dos pacotes no control point: mediana 1005 ms, média 1506 ms.**
Só 6,2% dos intervalos passam de 2000 ms — número que importa na seção 5.2.

### 4.3 ERG — o que a setting `zwift_erg` realmente faz ⚙️

Ela **não liga o ERG**. O caminho `0x05` → `changePower()` →
`resistanceFromPowerRequest()` está sempre ativo. O que ela faz é **bloquear o
caminho concorrente**:

- `characteristicwriteprocessor.cpp:18` — em `changeSlope()`, o `changeResistance()`
  derivado da inclinação só roda `if (force_resistance && !erg_mode)`
- `characteristicwriteprocessor2ad9.cpp:22` — idem para `SET_TARGET_RESISTANCE_LEVEL`

Sem ela, os dois caminhos escrevem no mesmo `requestResistance` e o `update()` grava
o que estiver lá. Como o MyWhoosh manda simulação a cada ~1 s, ela vence sempre.

Confirmação do mantenedor em [#4777]: *"qz wasn't applying erg because that setting
was disabled"*. E em [#3300]: *"Yes enable it for erg"*. Para free ride, [#3230]:
*"Zwift workout erg mode must be off for this"*. **É um liga/desliga por tipo de
sessão.**

O QZ **anuncia** ERG ao MyWhoosh (`0x2ACC` target features `0x0000E00C`, bit 3 —
`virtualbike.cpp:139`), então a recusa não vem do QZ.

### 4.4 A sessão 2 (treino estruturado) não teve controle de potência ✅

Percurso plano (1 m de subida em 4,09 km). Sem log do QZ (ver 5.3), mas o `.fit`
decide por dois testes:

| Teste | Sessão 2 | Sessão 1 (simulação, referência) |
|---|---|---|
| corr(cadência, R inferida) | −0,04 (destend. −0,06) | −0,14 (destend. +0,02) |
| W/rpm p5 / p50 / p95 | 1,026 / 1,197 / **1,395** | 1,323 / 1,778 / 2,689 |

R=13 corresponde a 1,385 → **~95% do treino rodou em R ≤ 13**, na zona morta. E
nenhuma assinatura de ERG: a resistência jamais perseguiu a cadência.

Causa: `zwift_erg` estava **false** no dump de settings, e o app não foi reiniciado
entre as sessões.

---

## 5. Defeitos e anomalias

### 5.1 Marchas em dobro ⚙️✅
Seção 3.2. **Contornado** desligando `gears_from_bike` e mantendo marcha 0.
Correção de código possível mas de baixa prioridade — o tile de dificuldade
(3.3) já cobre o caso de uso.

### 5.2 Keep-alive do ERG suprimido ⚙️✅ — **corrigir**

`virtualbike.cpp:1499` reaplica a última potência quando o app fica quieto:

```cpp
// zwift with the last update, seems to sending power request only when it actually
// wants to change it  →  so i need to keep this on to the bike
if (whenLastFTMSFrameReceived() > 0 &&
    (now > lastFTMSFrameReceived + 2000) && erg_mode) { ... }
```

Mas `lastFTMSFrameReceived` é atualizado por **qualquer** escrita no `0x2AD9`
(`virtualbike.cpp:664`), inclusive os pacotes de simulação. Com o MyWhoosh a
~1005 ms de mediana contra um limiar de 2000 ms, **só 6,2% dos intervalos disparam
o watchdog**.

Efeito: mesmo com `zwift_erg` ligado corretamente, o ERG ajusta a resistência uma
vez por bloco e congela — **não persegue a cadência**, que é o ponto do ERG.

### 5.3 Log truncado ❓ — investigar

O log parou em `12:17:17` com **40.509.090 bytes**, enquanto o app seguiu vivo até
~12:40 (backups do QZ têm mtime 12:40). **Não há limite de tamanho no código** — o
`maxDebugLogAttachmentBytes` de 10 MB em `homeform.cpp:10807` é só para anexo de
e-mail. Causa desconhecida; suspeita de storage/SAF do Android.

**É bloqueante para o plano**: sem log completo, toda sessão de teste perde a cauda,
que é onde estão as respostas.

---

## 6. Calibração

### 6.1 Estado das settings

| Setting | Valor no dia das sessões | Alvo |
|---|---|---|
| `bike_resistance_offset` | 12 | **18** |
| `bike_resistance_gain_f` | 1 | 1 |
| `gears_from_bike` | true | **false** |
| `gears_current_value_f` | 0 | 0 |
| `tile_erg_mode_enabled` | false | **true** |
| `zwift_erg` | false | false em free ride, **true** em treino |
| `virtualbike_forceresistance` | true | true |
| `zwift_erg_filter` / `_down` | 10 / 10 | 10 / 10 |
| `watt_gain` / `watt_offset` | 1 / 0 | 1 / 0 |
| `CRRGain` / `CWGain` | 0 / 0 (default) | 0 / 0 |

Preparado em `Documents/QZ/settings/megagym-calib.qzs` (5 chaves, aplica só o que
contém). **Ainda não aplicado** na última verificação — o tile *Target R.* mostrava
`100% @0%=13`.

**Importar:** ícone do QZ no canto superior esquerdo → **Settings** → aí aparece o
ícone de bandeja com seta para cima na barra de cima → **"Settings folder"** →
tocar no arquivo **duas vezes**. Depois **reiniciar o QZ** — o `bikeResistanceOffset`
é lido só no arranque (`main.cpp:649`).

**Ou na mão:** "Zwift Resistance Offset:" (`settings.qml:3685`) ·
"Change gears using knob (Experimental)" (`settings.qml:4707`) ·
Tiles Options → "Erg Mode" (`settings-tiles.qml:2683`).

**Conferência:** o tile *Target R.* mostra `difficult × gain + offset`. Deve passar a
`100% @0%=19`. Ele muda ao vivo, mas o comportamento só muda após reiniciar.

### 6.2 Por que offset 18

Sobre a demanda real (n=600), com a curva **medida** (não o ajuste linear):

| offset | ganho | R p5/p50/p95 | W p5/p50/p95 | Faixa W | Tempo em 32 |
|---|---|---|---|---|---|
| 12 (marcha 0) | 1,0 | 10/15/25 | 109/133/205 | 96 | 0,0% |
| **18** | **1,0** | **16/21/31** | **136/166/298** | **162** | 4,5% |
| 17 | 1,5 | 13/21/32 | 111/166/316 | 205 | 6,5% |

`offset 18` reproduz numericamente o que foi efetivamente pedalado na sessão 1
(offset 12 + marcha +1 = efetivo 18), que rendeu 146 W médios a 79,6 rpm — só que
agora sem depender de uma marcha ter caído em +1. E mantém tudo dentro da faixa útil
13–32.

`offset 12` com marcha 0 joga o p5 em R=10, dentro da zona morta.

A faixa de potência (162 W entre p5 e p95) depende **só do ganho**. Aumentar o ganho
briga com as marchas virtuais do MyWhoosh, que existem para achatar o relevo.

---

## 7. Plano de testes

Cada fase depende da anterior. Entregar sempre **o log do QZ** e, quando houver
MyWhoosh, **o `.fit`**.

### Fase 0 — Confiabilidade do log (bloqueante)
Abrir o QZ, deixar rodando ≥ 50 min, anotar a hora do fechamento.
**Aprova se** a última linha do log estiver a < 1 min do fechamento.
Se cortar de novo, anotar o tamanho: ~40,5 MB indica reprodutibilidade (ver 5.3).

### Fase 1 — Aplicar settings e conferir
Importar/ajustar, **reiniciar**, conectar a bike.
**Aprova se**: *Target R.* = `100% @0%=19`; tile *Erg Mode* presente; e apertar
paddles **não** produz `setGears` nem `gears_from_bike APPLIED` no log.

### Fase 2 — Calibração da `ergTable` (QZ sozinho, sem MyWhoosh) ★
A tabela hoje cobre bem só R 13–27, e o ERG falha justamente fora disso.

Pelo tile **Resistance**, subir em degraus **R = 5, 8, 11, 14, 17, 20, 23, 26, 29, 32**,
**~2 min cada**, com a cadência travada em ~80 rpm (±2 rpm). Depois repetir a escada
em **~65 rpm** e em **~95 rpm**, 1 min por nível.

*Por que 2 min:* a `ergTable` exige **10 amostras no mesmo par exato
(cadência, resistência)** (`ergtable.h:37`) e descarta o primeiro segundo após cada
troca (`ergtable.h`, `collectData`). Cadência oscilante espalha as amostras em vários
baldes e nenhum fecha.

**Aprova se** o log tiver `Added/Updated point` cobrindo os 10 níveis nas 3 cadências.
**Rende:** a curva real W/rpm × resistência, substituindo a da seção 2.3 nas pontas.

### Fase 3 — Free ride no MyWhoosh com o offset novo
~20 min, percurso com relevo, sem tocar nos paddles.
**Aprova se**: zero `setGears`; nenhum `exceeds max_resistance` atribuível a marcha;
histograma de resistência centrado em ~19–21, sem 42% num nível só.

### Fase 4 — Baseline de ERG ★
Requer a Fase 2. Ligar **Erg Mode** pelo tile. Treino estruturado de 10–15 min com
**um bloco estável de ≥ 5 min**; dentro dele variar a cadência de propósito:
2 min a ~70 rpm, 2 min a ~95 rpm, 1 min a ~70 rpm.

**Espera-se reprovar** em "potência segue o alvo" e "corr(cadência, R) negativa" —
é exatamente a evidência do defeito 5.2.
**Rende:** intervalo real entre os `0x05`, se o `0x11` continua durante o treino, e a
constante `hold` da Fase 6 medida em vez de arbitrada.

### Fase 5 — Corrigir o keep-alive (5.2)
Passar o watchdog de `virtualbike.cpp:1499` a usar o timestamp do último `0x05` em
vez de `lastFTMSFrameReceived`. Build pela VM (`C:\VMs\qz-build\README.md`).
Repetir a Fase 4.
**Aprova se**: corr(cadência, R) claramente negativa; potência dentro de ±10 W do
alvo (o `zwift_erg_filter`) durante os swings de cadência.
**Regressão:** free ride de 5 min sem mudança de comportamento.

### Fase 6 — Auto-ERG (feature nova)

Evita ter que ligar/desligar `zwift_erg` a cada sessão.

| Arquivo | Mudança |
|---|---|
| `characteristicwriteprocessor.h` | `qint64 lastPowerTargetRequest = 0` + `bool ergActive()` |
| `characteristicwriteprocessor2ad9.cpp` | gravar timestamp no `FTMS_SET_TARGET_POWER`; soltar o latch se potência = 0 ou vier `FTMS_STOP_PAUSE` |
| `characteristicwriteprocessor.cpp` | `changeSlope()`: `!erg_mode` → `!ergActive()` |
| `virtualbike.cpp:1499` | keep-alive gateado por `ergActive()` |

```cpp
bool CharacteristicWriteProcessor::ergActive() {
    QSettings settings;
    if (settings.value(QZSettings::zwift_erg, QZSettings::default_zwift_erg).toBool())
        return true;                        // modo manual, como hoje
    if (!settings.value(QZSettings::zwift_erg_auto, QZSettings::default_zwift_erg_auto).toBool())
        return false;
    if (lastPowerTargetRequest <= 0)
        return false;
    const int hold = settings.value(QZSettings::zwift_erg_auto_hold,
                                    QZSettings::default_zwift_erg_auto_hold).toInt();
    return (QDateTime::currentMSecsSinceEpoch() - lastPowerTargetRequest) < (qint64)hold * 1000;
}
```

Settings novas: `zwift_erg_auto` (bool, default false) e `zwift_erg_auto_hold`
(int, segundos — **valor medido na Fase 4**). Plumbing: `qzsettings.h`/`.cpp` (com
`allSettingsCount`), `settings.qml`, `settings-catalog.json` mantido à mão com
`settingCount` sincronizado, e teste em `tst/`.

**Teste:** `zwift_erg` off, `zwift_erg_auto` on — free ride 5 min (inclinação
controla) → iniciar treino (ERG engata no primeiro `0x05`) → bloco de ≥ 5 min (ERG
**não** cai no meio) → encerrar (volta à inclinação dentro do `hold`).
**Regressão obrigatória:** com `zwift_erg_auto` off, comportamento idêntico ao atual.

> Procurado no upstream: **não há nada parecido nem pedido aberto.** Seria feature
> nova do fork.

**Caminho crítico:** 0 → 1 → 2 → 4. As fases 3, 5 e 6 são validação e código.
Se for para encurtar, a **Fase 2 é a que mais rende**.

---

## 8. Dados de referência das sessões

| | Sessão 1 — *Dust Trail* | Sessão 2 — *Hudayriyat Octopus Loop* |
|---|---|---|
| Janela (UTC) | 14:59:43 → 15:24:35 | 15:25:59 → 15:36:13 |
| Duração | 1493 s | 615 s |
| Potência méd / máx | 146 / 353 W | 99 / 457 W |
| Cadência méd | 78 | 78 |
| Distância / subida | 10 709 m / 171 m | 4 089 m / 1 m |
| Log do QZ | 1055 s de sobreposição | **nenhum** (ver 5.3) |
| Modo | free ride, marchas virtuais | treino estruturado, sem ERG efetivo |

Log usado: `debug-Sun_Aug_2_11_55_19_2026` (40,5 MB, 11:55:19 → 12:17:17 local, UTC−3).

Validação do caminho de dados, 1055 s pareados:

| | bike (`0x2AD2`) | QZ→MyWhoosh | MyWhoosh `.fit` |
|---|---|---|---|
| Potência méd | 150,05 W | 150,05 W | 149,38 W |
| Cadência méd | 79,61 | 79,61 | 79,61 |

Delta médio −0,67 W (reamostragem 1 Hz, não viés).

---

## 9. Referências de código

| O quê | Onde |
|---|---|
| Fórmula terreno→resistência | `src/characteristics/characteristicwriteprocessor.cpp` (`changeSlope`) |
| Gate do `zwift_erg` (slope) | `characteristicwriteprocessor.cpp:18` |
| Gate do `zwift_erg` (0x04) | `characteristicwriteprocessor2ad9.cpp:22` |
| Roteamento do `0x05` | `characteristicwriteprocessor2ad9.cpp` (`FTMS_SET_TARGET_POWER`) |
| Emulação de ERG | `bike::changePower` (`devices/bike.cpp`), `ergtable.h` |
| Keep-alive furado | `virtualdevices/virtualbike.cpp:1499` + `:664` |
| Features anunciadas (`0x2ACC`) | `virtualbike.cpp:132-146` |
| Pacote `0x2AD2` do virtualbike | `characteristics/characteristicnotifier2ad2.cpp` |
| Marcha 1× | `devices/bike.cpp:73` |
| Marcha 5× | `devices/ftmsbike/ftmsbike.cpp:509` |
| `gears_from_bike` (paddle→marcha) | `ftmsbike.cpp:956` |
| Escrita de resistência + clamp | `ftmsbike.cpp:300-390` |
| Perfil YPBM | `ftmsbike.cpp:2171`, `bluetooth.cpp:2017` |
| Offset lido no arranque | `main.cpp:649` |
| Knob de dificuldade | `homeform.cpp:5317` |
| Import de settings | `SettingsList.qml`, `homeform.cpp:11053` |
| `ergTable` (10 amostras/par) | `ergtable.h:37` |

## 10. Fontes upstream

- [#4777](https://github.com/cagnulein/qdomyos-zwift/issues/4777) — *"qz wasn't applying erg because that setting was disabled"* (MyWhoosh)
- [#4525](https://github.com/cagnulein/qdomyos-zwift/issues/4525) — bikes sem ERG aprendem a tabela em free ride; ícone de ímã desliga a resistência automática
- [#4403](https://github.com/cagnulein/qdomyos-zwift/issues/4403) — *"Do a long ride without erg… leave the resistance automatic so qz will learn"*
- [#3300](https://github.com/cagnulein/qdomyos-zwift/issues/3300) — *"Yes enable it for erg"*; marcha deve estar em 0
- [#3230](https://github.com/cagnulein/qdomyos-zwift/issues/3230) — offset: *"suggest a mid value like 10 or 15"*; erg off para free ride
- [#3431](https://github.com/cagnulein/qdomyos-zwift/issues/3431) — marchas virtuais vs sensação no MyWhoosh

## 11. Armadilhas

- **O ícone de ímã** no canto superior direito desliga a resistência automática ([#4525]).
- **A marcha precisa estar em 0** para o ERG funcionar ([#3300]).
- **`settings-catalog.json` é manual** — `settingCount` sincronizado no mesmo commit.
- **Ordem dos padrões em `bluetooth.cpp`** — primeiro match vence; há padrões YPOO antes (`:1249`, `:1680`).
- **A bike aceita uma conexão por vez.** Fechar QZ/MyWhoosh antes de rodar `tools/paddle_logger.py`.
- **`speed_power_based = true`** — o QZ calcula velocidade própria (29,3 km/h méd) diferente da do MyWhoosh (25,6). Não afeta o jogo, mas o `.fit` do QZ não bate com o do app.
- **Build Android**: androiddeployqt não empacota as `.so` de forma confiável; gradle exige Java 17; `.qm` são artefatos não versionados. Ver `AGENTS.md` e `C:\VMs\qz-build\README.md`.

## 12. O que foi descartado do plano anterior

- **"A bike tem métricas completas"** — tem, mas a potência é sintética (2.2).
- **"Nada a implementar"** — a detecção está certa, mas há calibração real e dois defeitos (5.1, 5.2).
- **A seção sobre OpenBikeControl/mDNS** — irrelevante: o MyWhoosh conectou por BLE ao virtualbike do QZ e funcionou; não houve necessidade de descoberta por rede.
- **A hipótese de que a bike espera inteiro puro** na escrita de resistência — refutada; ×10 está correto e já é o que o QZ faz.
