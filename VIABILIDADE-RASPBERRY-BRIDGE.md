# QZ no Raspberry Pi e bridge com fio na megagym — viabilidade

> **Documento de viabilidade, não plano de execução.** Avalia rotas, custos, riscos e
> incógnitas para dois objetivos, e entrega os critérios que decidem entre eles. Não
> há fases numeradas nem cronograma: as decisões que os determinariam ainda não têm
> evidência.
>
> Complementa `PLANO-MEGAGYM.md` (branch `docs/plano-megagym`), que caracterizou a
> bike YPOO `YPBM001264` e diagnosticou os defeitos. Este documento herda os fatos de
> lá e não os repete — só os cita.

**Convenção:** ✅ medido · ⚙️ verificado no código · ❓ desconhecido. Toda afirmação
carrega marca; a ausência de marca é erro de redação.

---

## 1. TL;DR

1. **Migrar o QZ para o Pi é caminho batido.** O CI já produz e testa binários armv6hf
   e aarch64 a cada push. ⚙️
2. **O preço da migração é a interface.** O Pi não compila o servidor HTTP — sem GUI e
   sem web UI, a configuração vira arquivo `.conf` e flags de CLI. ⚙️
3. **O maior prêmio do bridge não é resistência, é o rádio.** Um único `hci0` fazendo
   central e peripheral ao mesmo tempo é o risco central da migração; tirar a bike do
   BLE resolve. ⚙️❓
4. **A bike é self-generating, confirmado.** O console acende sem tomada ✅. O chicote
   carrega energia, não só sinal: isso eleva o risco da caracterização e obriga o bridge
   a ter alimentação própria. Abre também a possibilidade de ler cadência pela frequência
   do gerador, de forma passiva ❓.
5. **O QZ já tem toda a infraestrutura para uma bike por serial.** Cinco devices seriais
   em produção, transporte `termios` cru sem dependência nova, e um ponto de entrada que
   ganha prioridade sobre a detecção BLE da bike. ⚙️
6. **O upstream tem três pedidos de bridge de hardware e zero entregas.** O que chegou
   mais longe — Trixter X-Dream, protocolo completo e PR — está aberto e não mergeado. ⚙️
7. **O bridge não produz watt real.** Nenhuma variante dele produz. Só pedivela ou pedal
   com extensômetro. ✅
8. **O sinal de comando de resistência existe no chicote, por dedução.** O console
   comanda o atuador eletronicamente ✅, logo manda algum sinal. A questão não é *se* dá
   para escrever, é **qual a forma** — e disso depende o custo do MITM (§3.3.2). ✅
9. **Há três protocolos de barramento documentados como arte prévia** — Trixter (serial
   115200), Freebeat (UART 9600) e Peloton (**RS-232 19200**). Nenhum é da YPOO ⚙️. A
   lição transferível da Peloton é elétrica, não arquitetural: chicote de console **não é
   TTL**. ⚙️
10. **O nível reportado está desacoplado da posição física ✅.** A potência publicada é
   calculada sobre o alvo, não sobre o estado dos ímãs. Isso esvazia os critérios de
   potência das Fases 4 e 5 do `PLANO-MEGAGYM` — o laço do ERG fecha sobre o próprio
   comando do QZ (§3.4). Não afeta a `ergTable`.
11. **Há uma decisão de ordem que custa trabalho se for ignorada:** calibrar a `ergTable`
   antes de decidir a escala de resistência é jogar a calibração fora — a menos que a
   rota escolhida preserve a escala 1–32. ⚙️

---

## 2. Objetivo 1 — QZ no Raspberry Pi

### 2.1 O que já existe ⚙️

| Job | Alvo | Onde |
|---|---|---|
| `raspberry-pi-build` | armv6hf, valida `Tag_CPU_arch = v6` (Pi Zero W) | `.github/workflows/main.yml:1462` |
| `raspberry-pi-build-and-image-64bit` | aarch64, valida GLIBC ≤ 2.31 (bullseye) | `main.yml:1525` |
| `raspberry-pi-smoke-test` | checa ELF e roda `-smoke-test` sob QEMU | `main.yml:1585` |

Baixar o artefato `raspberry-pi-binary-aarch64` do último run do master dispensa as
~45 min de compilação descritas em `docs/10_Installation.md:128`. O `docs/10_Installation.md:132`
aponta para um run fixo e antigo — usar o run corrente é estritamente melhor. ⚙️

### 2.2 Custo real: não há interface ⚙️

`src/qdomyos-zwift.pri:6` compila `webserverinfosender.cpp` apenas sob
`qtHaveModule(httpserver)`. Debian bullseye não traz o módulo, e o CI do Pi ainda o
remove explicitamente (`main.yml:1499` e `:1559`, mais o comentário em massa dos
`#include <QtHttpServer` em `:1500` e `:1560`).

Resultado: **binário de Pi sem web UI**, e em modo headless sem GUI. Sobra:

| Via | Referência | Observação |
|---|---|---|
| `qDomyos-Zwift.conf` editado à mão | `docs/10_Installation.md:213` | `/root/.config/'Roberto Viola'/` |
| Flags de CLI | `src/main.cpp:405` `-bike-resistance-offset`, `:401` `-bike-resistance-gain`, `:311` `-test-resistance`, `:373` `-name`, `:323` `-only-virtualbike`, `:299` `-no-gui`, `:303` `-qml` | cobrem o essencial da calibração |
| `TcpClientInfoSender` | `src/tcpclientinfosender.h:7` | o outro `TemplateInfoSender`; **não** depende de QHttpServer, sobrevive no Pi |

**Impacto direto sobre o `PLANO-MEGAGYM`:** o trabalho de calibração descrito lá é
inteiramente conduzido por GUI — importar `megagym-calib.qzs` (§6.1), conferir o tile
*Target R.*, ligar o tile *Erg Mode*. Nada disso existe no Pi. O import de `.qzs` é
fluxo QML (`SettingsList.qml`, `homeform.cpp:11053`) ⚙️ e morre junto. O equivalente
headless é editar as cinco chaves no `.conf` e reiniciar — o `bikeResistanceOffset` é
lido só no arranque de qualquer forma (`main.cpp:649`) ⚙️.

Alternativa a considerar: Raspberry Pi OS **Desktop** com `-qml`, sacrificando o boot
enxuto e o overlay FS de `docs/10_Installation.md:424` em troca da GUI. É uma escolha
entre plataforma de operação e plataforma de calibração; não precisam ser a mesma
máquina nem a mesma imagem.

### 2.3 Risco central: um rádio, dois papéis ⚙️❓

No Pi o mesmo adaptador teria que ser **central** (falando com a bike) e **peripheral**
(sendo o virtualbike para o MyWhoosh) simultaneamente.

- `src/virtualdevices/virtualbike.cpp:444` usa `QLowEnergyController::createPeripheral()`
  sem nenhum guard de plataforma ⚙️ — compila e roda no BlueZ.
- O QZ **não seleciona adaptador**: checa apenas `QBluetoothLocalDevice::allDevices()`
  não-vazio (`src/devices/bluetooth.cpp:152`) ⚙️ e usa o default.

Consequência prática: **um segundo dongle USB não resolve sozinho.** Resolveria com uma
mudança pequena — passar um `QBluetoothAddress` ao `QBluetoothDeviceDiscoveryAgent` para
prender o papel de central em `hci1`, deixando o peripheral no default. Custo estimado
baixo, mas não medido ❓.

Reportes upstream compatíveis com esse modo de falha, todos ❓ quanto a se aplicam a
esta configuração:

| Issue | Sintoma |
|---|---|
| [#1702](https://github.com/cagnulein/qdomyos-zwift/issues/1702) | para de anunciar depois de um tempo ocioso; reboot recupera |
| [#3041](https://github.com/cagnulein/qdomyos-zwift/issues/3041) | `-no-gui` não desconecta o BLE ao sair |
| [#3107](https://github.com/cagnulein/qdomyos-zwift/issues/3107) | velocidade não atualiza, erros intermitentes |

### 2.4 O que a migração provavelmente conserta ❓

O defeito §5.3 do `PLANO-MEGAGYM` — log truncado em 40.509.090 bytes com o app ainda
vivo — foi diagnosticado lá como sem causa no código, com suspeita de storage/SAF do
Android. No Pi é filesystem comum mais `journalctl`. A hipótese é que o defeito
desapareça; não há como confirmar antes de rodar. Se confirmar, o item que hoje é
descrito como bloqueante deixa de existir.

### 2.5 Balanço

| | |
|---|---|
| **Viável** | Sim. Binários prontos, testados em CI, documentação de instalação existente. |
| **Custo principal** | Perda de interface (§2.2). Requer decidir onde se calibra. |
| **Risco principal** | Coexistência central/peripheral no mesmo rádio (§2.3). |
| **Mitigações** | Bridge com fio (elimina o papel de central); ou seleção de adaptador com dois dongles; ou aceitar o risco e monitorar. |

---

## 3. Objetivo 2 — Bridge com fio

### 3.1 O que o QZ já oferece ⚙️

Cinco devices por porta serial em produção: `computrainerbike`, `kettlerusbbike`,
`freebeatbike`, `csaferower`/`csafeelliptical` e `waterrowerusb`, instanciados em
`src/devices/bluetooth.cpp:931–1012`, cada um atrás de uma setting de porta.

- **Transporte sem dependência nova:** `open()` + `termios` cru
  (`src/devices/kettlerusbbike/KettlerUSB.cpp:337`, `KettlerUSB.h:31`). Não usa
  QtSerialPort. Nada que quebre o build enxuto do Pi. ⚙️
- **Prioridade favorável:** a cadeia serial está em `bluetooth.cpp:931`; o match `YPBM`
  que hoje cria o `ftmsbike` está em `bluetooth.cpp:2017`. Primeiro match vence, então um
  branch serial novo **ganha automaticamente** da detecção BLE da bike, sem desligar
  nada. ⚙️
- **Pegadinha:** toda a cadeia vive dentro de `bluetooth::deviceDiscovered()`
  (`bluetooth.cpp:542`) ⚙️. Devices seriais só nascem quando *algum* anúncio BLE aparece.
  Com o console ligado e anunciando, dispara sozinho; com o console fora do circuito, um
  Pi headless espera para sempre. É correção necessária em qualquer rota que remova o
  console.
- **Rota do atuador separado:** `bluetooth.cpp:3159–3184` conecta o `smartspin2k` como
  `ftmsAccessory` — um device que só recebe `resistanceChanged` e devolve
  `resistanceRead`, enquanto a telemetria vem de outro. ⚙️ Permite um primeiro protótipo
  sem escrever device novo.
- **Direct Connect:** o QZ embute servidor dircon (`src/qzsettings.h:1263`, porta base
  `:1266` = 36866) ⚙️ — quarta rota possível, sobre TCP.
- **Arte prévia interna:** `QZ_ESP32/QZ_ESP32.ino` (436 linhas) faz cliente BLE lendo
  FTMS `0x2AD2` e servidor BLE publicando Cycling Power. ⚙️

### 3.2 Arte prévia upstream ⚙️

| Referência | Estado | O que é |
|---|---|---|
| [#855](https://github.com/cagnulein/qdomyos-zwift/issues/855) + [PR #899](https://github.com/cagnulein/qdomyos-zwift/pull/899) | **ambos abertos** | Trixter X-Dream — bike por serial, protocolo completo, PR nunca mergeado |
| [#2629](https://github.com/cagnulein/qdomyos-zwift/issues/2629) | fechado | stepper + ESP32 substituindo knob manual |
| [#525](https://github.com/cagnulein/qdomyos-zwift/issues/525) | aberto | Raspberry Pi simulando botões de esteira |
| [#780](https://github.com/cagnulein/qdomyos-zwift/issues/780) | aberto | protocolo serial do Wahoo Kickr |
| [PR #4851](https://github.com/cagnulein/qdomyos-zwift/pull/4851) / [#4791](https://github.com/cagnulein/qdomyos-zwift/issues/4791) | aberto | QZ como listener de OpenBikeControl |
| [#500](https://github.com/cagnulein/qdomyos-zwift/issues/500) | aberto | SS2k + Schwinn: auto-resistência cai no meio do treino |

**Leitura:** pedidos de bridge de hardware existem há anos no upstream e nenhum foi
entregue. Não é falta de ideia — é falta de alguém com o hardware na mão.

> Os comentários de #2629 e o corpo de PR #4851 permanecem ❓: `api.github.com` passou a
> devolver 403 por rate limit e o MCP do GitHub desta sessão está restrito a
> `nickolas122/qz` (`add_repo` recusou o upstream por ser de outro owner). Títulos,
> estados e corpos iniciais foram lidos antes do limite.

#### Trixter X-Dream é o melhor template disponível ⚙️

Melhor que o `freebeatbike`, que era a referência óbvia. Do corpo de #855:

```
Bike → Host: 16 bytes (como 32 chars hex), a cada ~13 ms, serial 115200
  [0] 0x6a header · [1] steering · [3] posição do pedivela 1–60
  [4-5] freios (135–250, 250 = solto) · [8-9] BOTÕES (bitmap)
  [10-11] tempo de revolução do pedivela
  [12-13] tempo de revolução do VOLANTE     → RPM ≈ 576000/F
  [14] HR bpm · [15] checksum XOR

Host → Bike: 6 bytes binários, a cada ~10 ms
  header + nível + checksum · 251 NÍVEIS de resistência
```

Três decisões de projeto que o Freebeat não tem e que esta bike precisa:

1. **Tempo de revolução em vez de RPM pré-calculado** — deixa a média para o consumidor,
   muito melhor em cadência baixa.
2. **Botões no mesmo quadro da telemetria** — endereça os paddles sem passar por
   `gears_from_bike`, cujo defeito de contagem dupla está em `PLANO-MEGAGYM` §3.2
   (`devices/bike.cpp:73` +1×, `devices/ftmsbike/ftmsbike.cpp:509` +5×) ⚙️.
3. **251 níveis** em vez de 32.

`grep -rn trixter src/` → nada ⚙️. **O QZ não tem esse driver.**

#### Correção à §12 do PLANO-MEGAGYM ⚙️

Aquele documento descartou OpenBikeControl como "irrelevante". Correto quanto a
*resistência* — OBC não trata disso. Mas OBC é protocolo aberto para **dispositivos de
input** (Zwift Play/Click, Di2) falarem com apps de treino, via mDNS ou BLE, e há PR
aberto tornando o QZ listener. É o encaixe natural do problema dos paddles. A dispensa
foi ampla demais.

#### Arte prévia externa

[SHIFTR](https://github.com/JuergenLeber/SHIFTR) — ponte BLE↔Direct Connect em ESP32.
Prova de que o caminho ESP32 funciona neste ecossistema. ❓ quanto a reuso direto.

### 3.3 SmartSpin2k — o que serve, e o que não serve ⚙️

[SmartSpin2k](https://github.com/doudar/SmartSpin2k) é um projeto ESP32 maduro (~9.700
linhas em `src/`) que o QZ já suporta como `ftmsAccessory` (`bluetooth.cpp:3159–3184`) ⚙️.

**O que não serve: motorizar o knob.** É o propósito principal do projeto — stepper
girando o knob de bikes cuja resistência não é comandável eletronicamente. **A megagym
aceita resistência-alvo por FTMS** (`0x2ACC` bit 2, escrita ×10 em `ftmsbike.cpp:374`,
`PLANO-MEGAGYM` §2.1) ✅, então essa metade do projeto resolve um problema que esta bike
não tem. Descartada.

**O que serve: é o único repo examinado com um protocolo de barramento de console
documentado em código funcionando.**

#### O tap na UART do console da Peloton ⚙️

Terceiro template, ao lado do Freebeat (§3.1) e do Trixter (§3.2) — e o único dos três que
é **tap no chicote de console de uma bike comercial**, que é exatamente o cenário da
megagym.

Camada física (`src/Main.cpp:101`, `include/settings.h:197`, `:200`):

| | |
|---|---|
| Interface | UART do console, hardware serial 1 do ESP32 |
| Taxa | **19200 8N1** |
| Pinos (placa rev2) | RX = GPIO22, TX = GPIO21 |
| **Níveis** | **RS-232, não TTL** — a PCB V3 traz um **MAX3232** ⚙️ |

Protocolo (`lib/SS2K/include/Constants.h:88–97`, `src/Main.cpp:554–576`,
`lib/SS2K/src/sensors/PelotonData.cpp`):

```
Host → bike, 4 bytes, a cada 100 ms (AUX_SERIAL_DELAY), alternando os IDs:
  [ 0xF5 | ID | checksum | 0xF6 ]      checksum = (byte0 + byte1) % 256

  ID  0x41 cadência · 0x44 potência · 0x49 resistência · 0x4a resistência alt (não impl.)

Bike → host:
  [ 0xF1 | ID | payload_len | payload… | 0xF6 ]
  payload = dígitos ASCII em ORDEM INVERSA (decodifica de 2+len para baixo até 3)
  potência = valor / 10 · resistência 5–98 (settings.h:108, :112)
```

Quatro lições diretamente transferíveis para a megagym:

1. **Níveis não são TTL.** A PCB precisou de um MAX3232 ⚙️. Encostar GPIO de ESP32 num
   chicote de console sem medir tensão é como se queima a placa. Reforça a regra de
   §4.2.
2. **O barramento é polled.** O host precisa *pedir*; um tap puramente passivo pode não
   ver nada se ninguém estiver perguntando. Na megagym, se houver barramento, o console é
   quem pergunta — então escutar deve mostrar tráfego, mas a ausência de tráfego **não
   prova** ausência de barramento. ❓
3. **Payload em ASCII, não binário.** Nem todo console fala binário empacotado; o parser
   tem que estar aberto a texto.
4. **O barramento da Peloton é só-leitura porque não há o que escrever** — e isso *não*
   se transfere. Ver §3.3.1: uma revisão anterior deste documento tirou daqui a conclusão
   oposta e errada.

#### 3.3.1 Retratação: a Peloton não é evidência contra escrita ✅

Uma revisão anterior deste documento concluiu, do fato de o SS2K girar o knob da Peloton
em vez de escrever no barramento, que "nem um fabricante grande expõe escrita, logo é
prudente não assumir que a YPOO exponha". **A inferência é falsa** e a analogia não se
sustenta.

A **Peloton Bike** original tem resistência **mecânica**: girar o knob move fisicamente as
peças dentro da bike. **Não existe atuador eletrônico para comandar.** O SS2K gira o knob
porque **o knob é o atuador** — é o propósito declarado do projeto, tornar smart uma bike
burra. O barramento é só-leitura porque não há nada do outro lado para escrever, não
porque o acesso foi negado.

A **Peloton Bike+** é a outra categoria: o knob **gira sem fim porque só manda sinal**, e
a resistência é eletrônica com auto-follow. O SS2K não é feito para ela — ela não precisa.

**A megagym é da segunda categoria, e há duas evidências independentes disso:**

| Evidência | Marca |
|---|---|
| Aceita resistência-alvo por FTMS: `0x2ACC` bit 2, escrita ×10 (`PLANO-MEGAGYM` §2.1, `ftmsbike.cpp:374`) | ✅⚙️ |
| O knob gira sem batente — assinatura de encoder digital, exatamente como a Bike+ | ✅ |

#### 3.3.2 Dedução: o sinal de comando existe ✅

Segue de forma direta, e não é hipótese:

> Se o console controla a resistência eletronicamente, **algum sinal ele manda**. Um
> atuador existe, é comandado pelo console, e o comando percorre o chicote.

Isso reposiciona a incógnita. A pergunta **não** é "o chicote aceita escrita?" — é **qual a
forma do sinal**, e o quanto disso é preciso assumir para injetá-lo:

| Forma do comando | Dificuldade de MITM |
|---|---|
| Posição absoluta a atuador inteligente (passo/serial/DAC) | **Baixa** — cortar um fio e comandar |
| Pulsos step/dir a driver de stepper | Baixa a média — injetar pulsos |
| Ponte H + realimentação de posição fechando no console | **Alta** — é preciso assumir motor *e* realimentação, ou seja, substituir a função de controle do console |

**Não há resultado "não dá para escrever".** Há um espectro de custo, e o ensaio da §3.3.3
diz onde nele a bike cai.

#### 3.3.3 Ensaio do salto 10→30 — e uma armadilha ✅

Reportado: mandando resistência-alvo por FTMS de 10 para 30, a bike **vai direto**, sem os
efeitos do knob (bipe, LEDs) ✅.

Bipe e LED **não servem de proxy** para movimento do atuador: pertencem ao tratador de
entrada do knob. O discriminador é o atraso até o pedal pesar — e ele foi medido.

### 3.4 O nível reportado está desacoplado da posição física ✅

**Confirmado:** em saltos grandes de resistência há atraso perceptível até o pedal pesar
✅. O console publica o **alvo** imediatamente no `0x2AD2` enquanto o ímã ainda está a
caminho.

É o fato mais consequente levantado depois do `PLANO-MEGAGYM`, e não é sobre o bridge.

#### 3.4.1 A potência é calculada sobre o alvo ✅

Explica a dispersão de 0,00 W em 99/99 combinações (`PLANO-MEGAGYM` §2.2) melhor do que
"tabela de firmware": não há **nada** físico no laço, nem sequer uma medição atrasada. O
console escolhe um número, publica esse número como resistência, e publica
`f(esse número, cadência)` como potência.

#### 3.4.2 Os critérios de ERG do PLANO-MEGAGYM ficam vazios ⚙️

Este é o impacto prático imediato. As Fases 4 e 5 daquele plano têm como critério
*"potência dentro de ±10 W do alvo"* e *"potência segue o alvo"*.

O caminho do ERG é: QZ escolhe R para a potência-alvo → escreve R na bike → a bike reporta
`f(R, cadência)` → QZ lê e compara com o alvo. **O laço fecha sobre o próprio comando do
QZ.** O critério passa por construção, independentemente do que aconteça com os ímãs.

Dos critérios daquelas fases, **só `corr(cadência, R)` negativa tem conteúdo** — essa mede
comportamento do controlador, não a aritmética do console. Os de potência precisam ser
descartados ou substituídos por medição externa.

#### 3.4.3 O que *não* é afetado ⚙️

A `ergTable` do QZ aprende `W = f(R_reportado, cadência)`, e a bike calcula
`W = f(R_alvo, cadência)` com `R_reportado == R_alvo`. As duas são a mesma função: **a
tabela aprendida é internamente consistente e não sofre contaminação por transiente.** A
janela de descarte de 1000 ms em `src/ergtable.h:112` ⚙️ é inofensiva aqui — protege
contra um problema que esta bike não tem, porque a potência nunca discorda da resistência
reportada.

Consequência: a Fase 2 do `PLANO-MEGAGYM` continua válida e produz a tabela correta. Ela
só não descreve física.

#### 3.4.4 O que fica pior do que se pensava ❓

O MyWhoosh manda simulação com mediana de 1005 ms (`PLANO-MEGAGYM` §4.2) ✅, e a demanda
percorre R 16–31 entre p5 e p95 com o offset 18 (§6.2 de lá) ✅. Se um salto grande leva
vários segundos, **o atuador persegue um alvo que se move mais rápido do que ele** —
possivelmente sem nunca chegar.

Nesse regime a resistência física é uma versão passa-baixa da demanda, enquanto o watt
reportado — e portanto o `.fit`, e o Strava — acompanha a demanda exatamente. A divergência
não é transiente: é o regime de operação normal em terreno com relevo. ❓ até medir o
atraso.

**Medida que falta:** segundos até o pedal pesar, para saltos de 1, 5 e 20 níveis. Custo
zero, e decide se §3.4.4 é nota de rodapé ou defeito central.

Detalhe de arquitetura que espelha o QZ: o SS2K injeta a fonte serial no seu pipeline de
sensores BLE dando a ela **UUID e endereço BLE sintéticos** — `PELOTON_DATA_UUID` e
`PELOTON_ADDRESS 00:00:00:00:00:00` (`Constants.h:85–86`) ⚙️. É o mesmo truque que o QZ
usa ao instanciar devices seriais dentro de `deviceDiscovered()` (§3.1).

#### O que o repo NÃO tem ⚙️

Verificado por busca, não por suposição:

- **Nada sobre YPOO ou YPBM.** `grep -rin "ypoo\|ypbm"` → zero ocorrências.
- Nada sobre bikes self-generating, geradores ou retenção de energia em console.
- Nenhuma leitura de sensor cru — sem hall, sem contagem de pulsos. O SS2K consome dados
  já decodificados (BLE) ou já formatados (UART da Peloton).
- Nenhuma escrita de resistência por barramento, em nenhuma bike.
- Fora a Peloton, todos os decodificadores de `lib/SS2K/src/sensors/` são BLE:
  `EchelonData`, `FlywheelData`, `CyclePowerData`, `CscSensorData`,
  `FitnessMachineIndoorBikeData`, `ChronoData`, `HeartRateData`.

#### Peças do SS2K que endereçam problemas deste documento ⚙️

| Peça | Endereça |
|---|---|
| `src/HTTP_Server_Basic.cpp` (854 linhas) — web UI no próprio ESP32 | Ataca de lado o custo do objetivo 1 (§2.2): se o bridge tem página de config própria, a configuração de resistência sai do QZ |
| `src/Power_Table.cpp` + `src/PowerTable_Helpers.cpp` (1069 linhas) | O mesmo problema da `ergTable` — ver §3.4 |
| `src/BLE_OpenBikeControl_Service.cpp` | Já fala OBC; casa com o PR #4851 aberto no QZ. Par pronto para o problema dos paddles |
| `src/DirConManager.cpp`, `src/DirConMessage.cpp` | Fala Direct Connect — mas ver a ressalva abaixo |
| TMC2209 + StallGuard (`Stepper.h`, `Stepper.cpp:221`) | Homing sensorless sem fim de curso, se algum dia for o eixo 2 |
| `BLE_Fitness_Machine_Service`, `_Cycling_Power_`, `_Cycling_Speed_Cadence_`, `_Wattbike_`, `_SB20_`, `_Zwift_` | Emulações prontas de referência |

**Direct Connect não ajuda como se esperaria.** O dircon do QZ é **só servidor**:
`src/devices/dircon/dirconprocessor.h:75` tem `QTcpServer *server`, e
`DirconProcessorClient` (`:63`) representa um socket de cliente *conectado*, não um
cliente de saída — não há `connectToHost` em `src/devices/dircon/*.cpp` ⚙️. **O QZ não
consome um device dircon.** A ideia de "SS2K por dircon libera o rádio do Pi" exige antes
escrever um cliente dircon no QZ ❓.

#### Bloqueio de licença ⚙️

**SS2K é GPL-2.0-only** (cabeçalhos SPDX `GPL-2.0-only`; `LICENSE` = GPLv2 de junho de
1991). **QZ é GPL-3.0** (`LICENSE:2`). GPL-2-only é incompatível com GPL-3: **não é
permitido copiar ou adaptar código do SS2K para dentro do QZ.**

Não impede: usar o SS2K como dispositivo — interoperar por BLE é uso, não derivação, e o
QZ já faz isso; ler o código para entender a abordagem; reimplementar de forma
independente.

### 3.5 `Power_Table` do SS2K contra `ergTable` do QZ ⚙️

| | QZ `ergTable` | SS2K `PowerTable` |
|---|---|---|
| Estrutura | pares exatos (cadência, resistência) | grade 2D `[10 cadências][30 watts]` (`settings.h:295`, `:298`) |
| Granularidade de cadência | valor exato | incremento de 5 rpm (`settings.h:307`) |
| Requisito de amostras | **10 no par exato** (`src/ergtable.h:37`) | agregação por balde da grade |
| Persistência | **sim**, QSettings (`src/ergtable.h:86`, `:90`) | sim, LittleFS |
| Extras | — | `predictWatts()`, checagens de confiabilidade, derivação de min/max do stepper |

Duas correções de premissa que esta comparação produziu:

1. **A `ergTable` do QZ persiste** ⚙️ — `loadSettings()` no construtor, `saveSettings()` no
   destrutor e a cada ponto novo. A calibração da Fase 2 do `PLANO-MEGAGYM` **não** é
   perdida entre sessões. Isso não estava verificado.
2. A diferença real é de **estrutura**, não de durabilidade. A grade de 5 rpm do SS2K é
   muito mais tolerante a cadência oscilante — que é exatamente a dificuldade que a Fase 2
   descreve ("cadência oscilante espalha as amostras em vários baldes e nenhum fecha").
   Como abordagem, é a melhor referência disponível ❓ quanto a valer o esforço de
   reimplementar no QZ.

---

## 4. O que há no chicote — a incógnita dominante

Dominante **para as rotas que invadem o chicote**. A rota da §3.3 não depende de nada
desta seção — é o argumento principal a favor dela.

Não existe documentação pública de chicote YPOO ❓. O que as listagens do fabricante
estabelecem sobre o F5:

| Fato | Fonte | Marca |
|---|---|---|
| "F5 **Self Generated Power**, volante 22 kg" | Made-in-China / ypoosport.com | ❓ (catálogo, não medido) |
| "**two-way magnetic brake system with electronic control** and self-generating electricity" | ypoosport.com | ❓ |
| Transmissão por correia | catálogo | ❓ |
| Consoles: Shuttle LED, 15,6" TFT, 21,5" TFT | catálogo | ❓ |
| **36 níveis** de resistência | catálogo | ❓ — conflita com §5 |

### 4.1 Três eixos, com pesos diferentes

**Eixo 1 — o chicote carrega energia. Confirmado ✅.** O console acende sem tomada ✅, logo
um gerador acionado por correia o alimenta. Três desdobramentos, um deles refutado:

- ~~O console morre quando se para de pedalar.~~ **Refutado ✅:** o console permanece
  ligado e mantém o BLE vivo depois que se para de pedalar. Existe retenção — bateria ou
  supercapacitor — no console. Isso **remove uma restrição de projeto** que eu havia
  imposto ao MITM, e implica que o console consegue acionar o motor de resistência com a
  bike parada.
- Há AC no chicote, possivelmente trifásica, com corrente real. **Não é um barramento de
  3,3 V que se mede displicentemente** — eleva materialmente o risco da caracterização e
  muda o ferramental (§7). ❓
- A frequência do gerador é proporcional à rotação do volante. **Pode não existir hall
  separado**; se a velocidade sai da própria AC, ler cadência é medir frequência — 100%
  passivo, sem tocar em nada. ❓

**Eixo 2 — sinais analógicos, cenário principal.** "Freio magnético bidirecional com
controle eletrônico" mais um console que monta tabela de potência apontam para
inteligência centralizada no console e sinal cru no chicote. Não há protocolo para
farejar. O bridge teria que acionar o atuador com feedback de posição e homing, o que na
prática tira o console do circuito de controle. Subvariantes ❓: DC + potenciômetro,
stepper com batente, ou servo.

**Contagem de pinos: 5 a 8 ✅.** Cai exatamente na banda "sensor + atuador + feedback" da
§4.2, e é consistente com o eixo 2. Descontando 2–3 pinos para o gerador, sobram 3–5:
suficiente para motor (2) + hall (2), ou motor (2) + potenciômetro (3), ou stepper (4)
com terra compartilhado.

**Eixo 3 — barramento digital, hipótese secundária.** Se existir placa controladora
embaixo conversando por UART/I²C/CAN, o MITM é o caso fácil e o console segue vivo. Foi
rebaixado de cenário coequivalente para hipótese secundária pela evidência dos eixos 1 e
2. A contagem de 5–8 pinos **não o mata** — alimentação (3) mais UART (3) cabe na faixa —
mas o exige dividindo espaço com os fios do gerador, o que aperta. ❓

**O discriminador que resta é um só:** quais pinos carregam AC enquanto se pedala.
Identificada a dupla (ou trinca) do gerador, o que sobra decide entre eixo 2 e eixo 3 por
simples contagem.

### 4.2 Ensaios que discriminam

Ordenados por custo. Nenhum exige código.

Os dois marcados **feito ✅** já foram executados; os resultados estão na §4.1.

| Ensaio | Ferramenta | O que decide |
|---|---|---|
| ~~Contar pinos do conector~~ — **feito ✅: 5 a 8** | nenhuma | banda "sensor + atuador + feedback"; ver §4.1 |
| Tensão DC de repouso por pino contra GND | multímetro | separa rails de sinal |
| **Tensão AC e frequência por pino, pedalando** | multímetro com frequencímetro | confirma o gerador e diz se a velocidade sai dele |
| Qual pino pulsa pedalando devagar | multímetro | localiza hall/reed, se existir |
| O que muda ao varrer R 1→32 | multímetro | tensão linear = pot/DAC; par que inverte = DC + ponte H; quatro fases = stepper; trem de bits 3,3 V = barramento |
| ~~A bike acende o console sem tomada?~~ — **feito ✅: sim, e o console segue ligado com o BLE vivo depois de parar de pedalar** | nenhuma | self-generating confirmado; há retenção de energia no console |
| Foto da placa do console: L298/DRV88xx/TB6612 presente? | nenhuma | ponte H no console = eixo 2 confirmado |
| Captura do barramento, se houver | analisador lógico | fecha o protocolo (baud típico 9600/19200/115200) |

**Regra de segurança:** só medição passiva primeiro. Não injetar nada antes de entender.
O console é a peça mais cara e menos substituível da bike, e o eixo 1 diz que há energia
real circulando ali.

---

## 5. A incógnita dos 32 níveis

O console expõe **32 níveis** de forma consistente: `0x2AD6` declara min 1, máx 32, passo
1 (`PLANO-MEGAGYM` §2.1) ✅, e o knob e os paddles do painel também param em 32 ✅. O
catálogo do F5 anuncia 36 ❓.

Como as três superfícies do console concordam entre si, a hipótese de "o BLE expõe menos
que o painel" está descartada ✅. O teto, se existir, é a montante.

### 5.1 A curva tem estrutura que a física não explica ✅

Usando apenas os níveis bem amostrados da §2.3 — descartando R 6, 7, 8 (n = 5–7) e R 14
(n = 4), que são ruído:

| trecho | níveis | Δ W/rpm | por nível | n das pontas |
|---|---|---|---|---|
| R 1 → 13 | 12 | +0,388 | **0,032** | 24 · 19 |
| R 13 → 16 | 3 | +0,311 | 0,104 | 19 · 434 |
| R 16 → 22 | 6 | +0,492 | 0,082 | 434 · 66 |
| R 22 → 26 | 4 | +0,489 | 0,122 | 66 · 19 |

Dois intervalos de largura quase igual — R 1→13 e R 13→26 — e o segundo entrega **3,7×
mais watt por nível**. Os quatro pontos que ancoram a conta têm n = 24, 19, 434 e 19; não
é ruído de amostragem. ✅

Frenagem magnética varia com o inverso do quadrado do entreferro, o que produz curva
convexa e **suave**. O que foi medido tem um degrau em algum lugar entre R=13 e R=16,
precedido de platô. Degrau é assinatura de mapeamento por tabela, não de física de ímã.

**Ressalva que a §3.4 impõe a esta análise.** Com o desacoplamento confirmado ✅, sabe-se
que a curva inteira da §2.3 é leitura de uma tabela de firmware indexada pelo alvo — não
há nada físico nela em ponto nenhum. Logo o cotovelo prova que **a tabela** tem um
cotovelo, e **não diz nada sobre os ímãs**. A hipótese "o teto de 32 é escolha de
firmware" continua de pé; a pergunta separada — se existe zona morta *física* entre R 1 e
13 — tornou-se **inacessível por estes dados** ❓, e só se responde pelo tato ou com
medidor de potência externo.

**Pergunta de custo zero que ninguém fez ainda:** subindo R de 1 a 13 pedalando, muda
alguma coisa na perna? Se não muda, a zona morta é física. Se muda progressivamente, ela
é só da tabela.

### 5.2 Três hipóteses

| # | Hipótese | Implicação para o bridge |
|---|---|---|
| a | Mesmo curso mecânico, 32 rótulos em vez de 36 | ganha resolução, não ganha faixa |
| b | Passo fixo por nível, truncado em 32 | 32 níveis ocupam ~89% do curso; sobram 4 níveis no topo. Como R 27→32 corre a 0,226 W/rpm por nível, extrapolação grosseira põe R=36 perto de 450 W a 80 rpm ❓ |
| c | Console de SKU inferior, não rebranding | mesmo chassi, atuador capaz de ≥36 com firmware diferente |

(b) e (c) não são exclusivas e dizem a mesma coisa ao projeto: **há curso mecânico além
do que o console comanda.**

### 5.3 Dois ensaios de custo zero decidem

**Cronometrar o atuador.** Medir de ouvido quanto tempo **o motor** roda em 1→2, 12→13,
13→14 e 31→32 — o motor, não o console: bipe e LED pertencem ao tratador do knob e não
acompanham o atuador (§3.3.3) ✅. Tempo constante ⇒ mapeamento linear em posição e o cotovelo em 13 é tabela
de potência, não mecânica. 13→14 visivelmente mais longo ⇒ salto de posição no firmware,
confirma remapeamento. 31→32 mais curto que os vizinhos ⇒ 32 é o batente.

**Inspecionar o entreferro.** Fotografar o carro de ímãs em R=1 e em R=32. Observar
quanto gap sobrou em 32; se há batente (ruído seco, ou zumbido de stall se o motor
insistir); e onde o ímã começa a se aproximar de fato — se em R=13 o gap já estiver quase
fechado, o platô de 1–13 é ar e é físico.

---

## 6. O que o bridge não resolve

Explicitamente, porque a expectativa natural erra aqui.

| Não resolve | Por quê |
|---|---|
| **Potência real** | A potência é tabela de firmware — 99 de 99 combinações com dispersão 0,00 W (`PLANO-MEGAGYM` §2.2) ✅. No eixo 2 perde-se até essa tabela e é preciso inventar outra, igualmente sintética. O gerador é fonte de alimentação, não sensor — se fosse carga eletrônica, o firmware teria potência elétrica medida de graça e a dispersão não seria zero ✅. **Só pedivela ou pedal com extensômetro dá watt real.** |
| **Marcha em dobro** | `PLANO-MEGAGYM` §5.1 — defeito de código do QZ (`bike.cpp:73`, `ftmsbike.cpp:509`) ⚙️. Sobrevive ao bridge. |
| **Keep-alive do ERG furado** | §5.2 — `virtualbike.cpp:1499` gateado por `lastFTMSFrameReceived`, atualizado por qualquer escrita no `0x2AD9` (`:664`) ⚙️. Sobrevive ao bridge. |
| **Tempo físico do atuador** | Mecânica. |
| **Zona morta R 1–13** | ❓ **Rebaixado de ganho para incógnita.** A evidência está dividida: o cotovelo (§5.1) diz firmware, um eventual gap fechado em R=13 diria física. Os ensaios da §5.3 decidem. Se for física, nenhum bridge a recupera e comprar resolução ali não serve para nada. |

### 6.1 O que o bridge resolve, em ordem de valor

1. **Liberar o rádio BLE do Pi** ⚙️ — o único ganho que independe de tudo em §4 e §5, e o
   que amarra os dois objetivos. Também elimina a armadilha de "uma conexão por vez"
   (`PLANO-MEGAGYM` §11) ✅. **Só as rotas com fio o entregam**; a rota BLE-FTMS do §7.1
   não.
2. **Resolução dentro de R 13–32** ❓ — provável, se o atuador for contínuo ou mais fino
   que 32 passos.
3. **Faixa acima de R=32** ❓ — vale entre nada e ~130 W de topo. §5.3 decide.
4. **Estado físico real da resistência** ✅ — ganho novo, trazido pela §3.4. Um bridge que
   leia a posição do atuador (potenciômetro, encoder, contagem de passos) sabe onde o ímã
   **está**; o console só publica onde ele **deveria estar**. Não produz watt real, mas
   elimina a ficção do transiente e permite ao QZ saber quando a resistência chegou.
5. **Paddles limpos** ⚙️ — via quadro de botões (§3.2) ou OBC, contornando
   `gears_from_bike`.
6. **Latência determinística** no caminho de controle ❓.

---

## 7. Ferramental a adquirir

Ordem de grandeza no mercado BR; confirmar antes de comprar. Estado atual: nada
disponível.

| Item | Para quê | ~R$ |
|---|---|---|
| **Multímetro com frequencímetro** (Minipa ET-1002 ou similar) | O eixo 1 (§4.1) exige medir AC e frequência, não só DC. Se a velocidade sair do gerador, o frequencímetro lê cadência na bancada sem escrever código. Um DT-830B básico **não** basta | 80–120 |
| Analisador lógico USB 8 ch 24 MHz (clone Saleae) | Fecha o eixo 3 se ele aparecer. Funciona com sigrok/PulseView, livre, roda no próprio Pi | 35–60 |
| **ESP32 DevKit** (WROOM-32) | Ver §7.1 | 35–60 |
| Protoboard, jumpers, garras jacaré | Medir sem cortar fio | 20–40 |
| Level shifter bidirecional 3,3/5 V | Seguro se o barramento for 5 V | 10–20 |
| Pi Zero 2 W + cartão SD, se não houver Pi | Objetivo 1 | 250–400 |

Os ensaios de §5.3 e metade dos de §4.2 não precisam de nada — podem preceder a compra.

### 7.1 ESP32, não Arduino

- Lógica nativa 3,3 V, que casa com barramento de console sem level shifter na maioria
  dos casos.
- Periférico **PCNT** em hardware: conta pulsos sem consumir CPU nem interrupção — o
  gargalo exato de um AVR contando hall enquanto conversa por serial.
- **Arte prévia dentro de casa:** `QZ_ESP32/QZ_ESP32.ino` ⚙️, e abre a rota de o bridge
  falar BLE-FTMS direto.

**Contrapartida da rota BLE-FTMS:** se o bridge se anunciar como bike FTMS padrão, o QZ o
adota com o `ftmsbike` que já existe — **zero código novo no QZ** ⚙️. Mas ela reintroduz
BLE e portanto **não entrega o ganho nº 1 da §6.1**, que é o que motiva o projeto. Troca
esforço de software pelo prêmio principal. Vale como protótipo, não como destino. A mesma
contrapartida se aplica à rota SS2K da §3.3.

### 7.2 Item que a lição da Peloton acrescenta

Se o chicote tiver barramento, ele pode não ser TTL — a PCB do SS2K precisou de um
**MAX3232** para falar com o console da Peloton (§3.3) ⚙️. Um transceptor RS-232 de
bancada (MAX3232 em módulo, R$10–20) entra na lista como item condicional, a comprar
**depois** de medir as tensões, não antes.

---

## 8. Interação entre os dois objetivos

São independentes em execução e acoplados em valor:

- O objetivo 1 tem um risco (§2.3) cuja melhor mitigação é o objetivo 2.
- O objetivo 2 não depende do 1 — pode ser desenvolvido com o QZ ainda no Android, onde
  há GUI para depurar.
- A caracterização de hardware (§4.2, §5.3) não depende de nenhum dos dois e é a mais
  barata das três frentes.

### 8.1 Restrição de ordem ⚙️

**A `ergTable` é indexada por nível de resistência.** `ergtable.h:37` exige 10 amostras no
mesmo par exato (cadência, resistência) ⚙️. A calibração descrita em `PLANO-MEGAGYM` §7
Fase 2 — dez níveis × três cadências, ~2 min por ponto — produz uma tabela sobre a escala
1–32.

**Se o bridge trocar a escala de resistência, essa tabela é descartada.** A restrição é
condicional à rota: na rota SS2K (§3.3) **o console segue no comando e a escala continua
1–32**, então a calibração sobrevive e a Fase 2 deixa de ser refém. Nas rotas que
substituem o controle do console, não.

| Trabalho do PLANO-MEGAGYM | Depende da escala? |
|---|---|
| Confiabilidade do log (§7 Fase 0) | Não — e provavelmente evapora no Pi (§2.4) |
| Settings, offset 18 (§6, §7 Fase 1) | Não — mas o *como* muda no Pi (§2.2) |
| Keep-alive do ERG (§5.2, §7 Fase 5) | Não — código puro, independe de tudo aqui |
| Auto-ERG (§7 Fase 6) | Não |
| **Calibração da `ergTable` (§7 Fase 2)** | **Sim — refém da decisão do bridge, exceto na rota SS2K** |
| MyWhoosh, free ride e ERG (§7 Fases 3 e 4) | Sim, por dependerem da Fase 2 |

O item de melhor custo-benefício que não é refém de nada é o keep-alive do ERG: defeito
já diagnosticado, correção localizada, independe de Pi, de bridge e de hardware.

---

## 9. Incógnitas, por quanto travam decisão

1. **Quantos segundos até o pedal pesar, por tamanho de salto?** (§3.4.4) Decide se o
   desacoplamento é nota de rodapé ou o defeito central desta bike: com demanda a ~1 Hz e
   excursões de 15 níveis, um atuador lento pode nunca chegar, e aí a resistência física é
   um passa-baixa da demanda enquanto o `.fit` registra a demanda. Custo: **zero**.
2. **O atuador tem curso além de R=32, e existe zona morta física em R 1–13?** (§5)
   Decide os ganhos 2 e 3 da §6.1. Custo: zero (§5.3), mais a pergunta de tato da §5.1.
   A parte "é firmware ou entreferro" ficou **inacessível pelos dados de potência** (§5.1).
3. **Existe barramento digital no chicote, e em que nível elétrico?** (§4, §3.3) Decide
   entre MITM barato e substituição do console, e decide se é seguro encostar um GPIO
   nele. A lição da Peloton diz que a resposta pode ser RS-232 ⚙️. Custo: um multímetro.
4. **Quais pinos carregam AC ao pedalar?** (§4.1) É o discriminador único que resta
   entre eixo 2 e eixo 3, agora que a contagem de 5–8 pinos ✅ está fechada. Custo: um
   multímetro.
5. ~~**A bike é mesmo self-generating, e o console sobrevive à parada?**~~ **Resolvido
   ✅** (§4.1): é self-generating, e o console **sobrevive** com o BLE vivo — há retenção
   de energia. O risco elétrico da caracterização permanece; a restrição de projeto sobre
   o MITM cai.
6. **A coexistência central/peripheral no `hci0` do Pi degrada em quanto tempo?** (§2.3)
   Decide se o bridge é necessário ou apenas desejável. Custo: uma sessão longa no Pi.
7. **O log truncado desaparece no Pi?** (§2.4) Decide se a Fase 0 do PLANO-MEGAGYM existe.
8. **Quanto custa prender o papel de central em `hci1`?** (§2.3) Alternativa barata ao
   bridge para o ganho nº 1; não avaliada.
9. **O que o mantenedor respondeu em #2629, e o que faz o PR #4851?** (§3.2) Pode conter
   orientação de arquitetura que evita retrabalho. Bloqueado por rate limit nesta sessão.
10. **Qual a forma do sinal de comando do atuador?** (§3.3.2) Que ele existe é dedução
   ✅; o que decide o custo do MITM é se é posição absoluta, step/dir, ou ponte H com
   realimentação fechando no console. Custo de estreitar: **zero** — o ensaio do salto
   10→30 da §3.3.3.
11. **A velocidade sai da frequência do gerador?** (§4.1) Se sim, existe leitura de
   cadência totalmente passiva e sem risco — o degrau natural para validar o caminho de
   dados serial ponta a ponta antes de qualquer coisa irreversível.

---

## 10. Referências de código

| O quê | Onde |
|---|---|
| Jobs de Pi no CI | `.github/workflows/main.yml:1462`, `:1525`, `:1585` |
| Remoção do QtHttpServer no Pi | `main.yml:1499`, `:1500`, `:1559`, `:1560` |
| Web UI condicional | `src/qdomyos-zwift.pri:6` |
| Info sender que sobrevive no Pi | `src/tcpclientinfosender.h:7` |
| Flags de CLI úteis sem GUI | `src/main.cpp:299`, `:303`, `:311`, `:323`, `:373`, `:401`, `:405` |
| Peripheral BLE | `src/virtualdevices/virtualbike.cpp:444` |
| Adaptador BLE não selecionável | `src/devices/bluetooth.cpp:152` |
| Entrada de descoberta | `src/devices/bluetooth.cpp:542` |
| Cadeia de devices seriais | `src/devices/bluetooth.cpp:931–1012` |
| Match YPBM (perde para a cadeia serial) | `src/devices/bluetooth.cpp:2017`, `src/devices/ftmsbike/ftmsbike.cpp:2171` |
| Outras famílias YPOO conhecidas | `bluetooth.cpp:1249` (`YPOO-U3-`), `:1680` (`YPOO-MINI PRO-`) |
| Atuador separado (`ftmsAccessory`) | `src/devices/bluetooth.cpp:3159–3184` |
| Transporte serial cru | `src/devices/kettlerusbbike/KettlerUSB.cpp:337`, `KettlerUSB.h:31` |
| Protocolo UART documentado | `src/devices/freebeatbike/PROTOCOL.md` |
| Direct Connect — **servidor apenas** | `src/qzsettings.h:1263`, `:1266`; `src/devices/dircon/dirconprocessor.h:75` |
| Driver do SS2K no QZ | `src/devices/smartspin2k/smartspin2k.cpp:57`, `:222` |
| Sketch ESP32 existente | `QZ_ESP32/QZ_ESP32.ino` |
| `ergTable`, 10 amostras por par | `src/ergtable.h:37` |
| `ergTable` persiste em QSettings | `src/ergtable.h:86`, `:90` |
| Licença do QZ (GPL-3) | `LICENSE:2` |

### 10.1 Referências no SmartSpin2k ⚙️

| O quê | Onde |
|---|---|
| Homing contra bike FTMS | `src/Stepper.cpp:291` (`_findFTMSHome`), despacho em `:389` |
| Homing sensorless por StallGuard | `src/Stepper.cpp:221`, `include/Stepper.h:12–27` |
| Tabela de potência | `src/Power_Table.cpp`, `src/PowerTable_Helpers.cpp`; dimensões em `include/settings.h:295`, `:298`, `:307` |
| Web UI no ESP32 | `src/HTTP_Server_Basic.cpp` |
| OpenBikeControl | `src/BLE_OpenBikeControl_Service.cpp` |
| Direct Connect | `src/DirConManager.cpp`, `src/DirConMessage.cpp` |
| Característica customizada (a que o QZ escreve) | `CustomCharacteristic.md`, `src/BLE_Custom_Characteristic.cpp` |
| **Tap RS-232 no console da Peloton** | `src/Main.cpp:101` (19200 8N1), `:554–576` (TX), `lib/SS2K/src/sensors/PelotonData.cpp` (decode) |
| Constantes do protocolo Peloton | `lib/SS2K/include/Constants.h:88–97` |
| Fonte serial com identidade BLE sintética | `lib/SS2K/include/Constants.h:85–86` |
| MAX3232 na PCB V3 | `Hardware/V3 - Integrated PCB/PCB/SS2K_KiCad_Files/SmartSpin2k.kicad_pcb` |
| Licença (GPL-2.0-only) | `LICENSE`, cabeçalhos SPDX |

## 11. Fontes externas

- [YPOO F5 Self Generated Power, volante 22 kg](https://ypoosports.en.made-in-china.com/product/LtdrucSBEqVi/China-Ypoo-Exercise-Bike-Spinning-Bike-F5-Self-Generated-Power-22kg-Flywheel-with-Ypoofit-APP.html)
- [YPOO — freio magnético bidirecional com controle eletrônico e autogeração](https://www.ypoosport.com/magnetic-spinning-bike)
- [F5 Commercial Spinning Bike — 36 níveis, 22 kg](https://www.megamallonline.store/products/pro-sportz-commercial-stationary-spinning-bike-with-apps-preorder-sales-now-open)
- [Patente 8328692 — aparato de resistência autogerativa](https://patents.justia.com/patent/8328692)
- [Patente 20200147449 — spinner com resistência magnética ajustável](https://patents.justia.com/patent/20200147449)
- [SmartSpin2k](https://github.com/doudar/SmartSpin2k) — firmware ESP32, GPL-2.0-only · [SS2k-Hardware](https://github.com/doudar/SS2k-Hardware) (CERN-OHL-P) · [SS2kConfigApp](https://github.com/doudar/SS2kConfigApp)
- [OpenBikeControl — protocolo](https://github.com/OpenBikeControl/openbikecontrol-protocol) · [bikecontrol](https://github.com/OpenBikeControl/bikecontrol)
- [SHIFTR — ponte BLE↔Direct Connect em ESP32](https://github.com/JuergenLeber/SHIFTR)
- [Wiki de compatibilidade do QZ](https://github.com/cagnulein/qdomyos-zwift/wiki/Equipment-Compatibility) — sem nenhuma entrada YPOO ⚙️
