# Plano de execução — megagym, do tablet ao Pi

> **Consolida** `PLANO-MEGAGYM.md` (branch `docs/plano-megagym`) e
> `VIABILIDADE-RASPBERRY-BRIDGE.md`. Não repete os fatos de lá — cita.
>
> Quatro etapas, cada uma com portão de saída. **Nenhuma etapa começa antes de a
> anterior passar**, porque cada uma é a referência de comparação da seguinte.

**Convenção:** ✅ medido · ⚙️ verificado no código · ❓ desconhecido.

---

## 0. Aviso que vale para o plano inteiro

**Nenhum critério de aceitação deste plano pode envolver "a potência bate com o alvo".**

A potência publicada pela bike é `f(nível-alvo, cadência)`, calculada sobre o alvo e não
sobre a posição dos ímãs (`VIABILIDADE` §3.4) ✅ — **provado pelo teste 1.5**, que fecha o
argumento sem instrumento externo. Qualquer laço que feche sobre ela fecha
sobre o próprio comando do QZ e passa por construção.

Onde o `PLANO-MEGAGYM` pede *"potência dentro de ±10 W do alvo"* ou *"potência segue o
alvo"* (Fases 4 e 5), **o critério é vazio e foi substituído aqui**. O que sobrevive é
`corr(cadência, R)`, que mede o controlador e não a aritmética do console.

Só um medidor de potência externo (pedal/pedivela, `VIABILIDADE` §7.3) tornaria os
critérios de watt falsificáveis. Nenhuma etapa deste plano depende dele.

---

## Etapa 1 — Tablet → MyWhoosh no PC, sem nada no meio

**Objetivo:** produzir a linha de base documentada. Tudo depois é comparado contra ela.

**Material:** nada novo.

### 1.1 Aplicar settings e conferir

Alvos em `PLANO-MEGAGYM` §6.1. As cinco que importam:
`bike_resistance_offset=18`, `bike_resistance_gain_f=1`, `gears_from_bike=false`,
`gears_current_value_f=0`, `zwift_erg=false`.

Importar `megagym-calib.qzs` (fluxo em §6.1 de lá) e **reiniciar o QZ** — o
`bikeResistanceOffset` é lido só no arranque (`main.cpp:649`) ⚙️.

| Aceita se | |
|---|---|
| Tile *Target R.* mostra `100% @0%=19` | ✅ |
| Apertar paddles **não** gera `setGears` nem `gears_from_bike APPLIED` no log | ✅ |

**Artefatos:** log do QZ · export `.qzs` pós-mudança (guardar, é a semente da Etapa 3).

### 1.2 Linha de base do caminho de dados ★

**A referência do plano inteiro.** Free ride de ~20 min no MyWhoosh, percurso com relevo,
sem tocar em paddles.

Extrair, como em `PLANO-MEGAGYM` §8:

| Medida | De onde |
|---|---|
| Potência e cadência médias na bike | `0x2AD2` no log do QZ |
| Potência e cadência médias QZ→MyWhoosh | log do QZ |
| Potência e cadência médias no app | `.fit` do MyWhoosh |

| Aceita se | |
|---|---|
| bike → QZ→MyWhoosh é 1:1, sem erro de escala | ✅ |
| `.fit` dentro de ~1 W da média enviada (referência: −0,67 W) | ✅ |
| Zero `setGears`; nenhum `exceeds max_resistance` | ✅ |
| Histograma de resistência centrado em ~19–21, sem 42% num nível só | ✅ |

**Artefatos:** log do QZ · `.fit` do MyWhoosh · as três médias tabuladas.

> O `.fit` do próprio QZ **não** vai bater com o do MyWhoosh: `speed_power_based=true` faz
> o QZ calcular velocidade própria (`PLANO-MEGAGYM` §11) ✅. Não é defeito. Comparar
> potência e cadência, nunca velocidade nem distância.

### 1.3 Atraso do atuador ★ — incógnita nº 1 do documento de viabilidade

**Custo zero, e é a medida de maior retorno de todo o plano.**

**Ferramenta: os tiles de preset de resistência.** O knob da bike anda de 1 em 1, mas o QZ
não passa por ele — `ftmsbike.cpp:374` escreve **um único pacote FTMS absoluto** para o
perfil YPBM ⚙️:

```cpp
uint8_t write[] = {FTMS_SET_TARGET_RESISTANCE_LEVEL, 0x00, 0x00};
write[1] = ((uint16_t)requestResistance * 10) & 0xFF;   // nível × 10, LE
write[2] = ((uint16_t)requestResistance * 10) >> 8;
```

Os tiles *preset resistance* chamam `changeResistance(<valor>)` com valor **absoluto**
(`homeform.cpp:4692–4726`) ⚙️, lido das chaves `tile_preset_resistance_1_value` …
`_5_value` (`qzsettings.cpp:525–545`) ⚙️. Configurar em Tiles Options e habilitar os cinco:

| Preset | Valor | Serve a |
|---|---|---|
| 1 | **1** | 1.4 (entreferro mínimo) |
| 2 | **13** | 1.3, 1.5.b |
| 3 | **10** | 1.5.a |
| 4 | **30** | 1.5.a |
| 5 | **32** | 1.3, 1.4, 1.5.b |

> `changeResistance` passa por `bike.cpp:73` — `resistência × m_difficult + gearsModifier()`
> ⚙️. Conferir **dificuldade em 100% e marcha em 0** antes de cada corrida, senão o valor
> comandado não é o do tile. A 1.1 já garante marcha 0; a dificuldade volta sozinha a 100%
> a cada sessão (`PLANO-MEGAGYM` §3.3) ✅.

Bike conectada, QZ mostrando resistência. Tocando os presets, comandar saltos e cronometrar
**duas coisas separadas**:

- `t_reportado` — do comando até o número mudar no QZ
- `t_sentido` — do comando até o pedal pesar de fato

Saltos de **1, 5 e 20 níveis**, três repetições cada, subindo e descendo.

> Cronometrar pelo **ruído do motor**, nunca por bipe ou LED: eles pertencem ao tratador de
> entrada do knob e não acompanham o atuador (`VIABILIDADE` §3.3.3) ✅.

**Não tem critério de aceitação — é caracterização.** A regra de decisão:

| Resultado | Consequência |
|---|---|
| `t_sentido` de 20 níveis < ~1 s | Desacoplamento é nota de rodapé |
| `t_sentido` > ~2 s | Com demanda a ~1005 ms de mediana e excursão de 15 níveis entre p5 e p95 (`PLANO-MEGAGYM` §4.2) ✅, **o atuador nunca chega**. A resistência física vira passa-baixa da demanda enquanto o `.fit` registra a demanda |

#### 1.3.a Knob contra FTMS — teste de brinde ★

O knob andar de 1 em 1 não atrapalha: **vira baseline de comparação.** Fazer o mesmo
13→32 das duas formas e comparar tempo total e ruído do motor:

| Via | Como |
|---|---|
| Knob | 19 cliques, o mais rápido que der |
| FTMS | um toque no preset 5 |

| Resultado | Leitura |
|---|---|
| FTMS faz **uma corrida contínua** do motor, mais rápida que os 19 cliques | O console recebe posição absoluta e executa um movimento só — comando absoluto no chicote, **MITM barato** |
| FTMS soa igual aos 19 cliques (rajadas discretas) | O console decompõe internamente em passos por nível |

Estreita a incógnita nº 10 da `VIABILIDADE` (qual a forma do sinal de comando do atuador)
sem abrir nada.

**Artefatos:** tabela de tempos · gravação de áudio das duas formas · log do QZ.

### 1.4 Bancada: curso, batente e cotovelo

Também custo zero. Três observações:

1. **Entreferro em R=1 e R=32** — fotografar. Quanto de gap sobrou em 32?
2. **Existe batente em 32?** Ruído seco, ou zumbido de stall se o motor insistir.
3. **Tempo de motor por transição:** 1→2, 12→13, 13→14, 31→32.

| Leitura | Significado |
|---|---|
| 13→14 nitidamente mais longo que os vizinhos | Salto de posição no firmware — confirma remapeamento |
| 31→32 mais curto | 32 é o batente; não há curso sobrando (`VIABILIDADE` §5.2 hipótese *a*) |
| Gap visível em 32 | Há curso além — hipóteses *b*/*c*, e o bridge ganha topo de escala |

**Artefatos:** fotos · tabela de tempos.

### 1.5 Prova do desacoplamento ★★ — sem instrumento externo

O §0 deste plano descarta todos os critérios de potência com base na afirmação de que
`W = f(R_alvo, cadência)`. Essa afirmação precisa de prova, e ela sai de dois testes que
juntos fecham o argumento. Nenhum exige compra.

**As três hipóteses a discriminar:**

| | |
|---|---|
| **H0** | `W = f(R_alvo, cadência)` — sobre o comando |
| **H1** | `W = f(R_físico, cadência)` — sobre a posição dos ímãs, por tabela |
| **H2** | `W` é medido (extensômetro, corrente do gerador, corrente do motor) |

A dispersão de 0,00 W em 99/99 combinações (`PLANO-MEGAGYM` §2.2) ✅ **já derruba H2** —
nenhuma medição tem ruído zero. Falta separar H0 de H1, e para isso é preciso um regime em
que `R_alvo ≠ R_físico`. O atraso do atuador (1.3) é exatamente esse regime.

#### 1.5.a Resíduo nulo no transiente — mostra que `W` só depende do reportado

Com o par (cadência, resistência) da tabela da 1.6 — ou da curva de `PLANO-MEGAGYM` §2.3 —
prever `W` a cada amostra a partir de **`R_reportado` e `cadência_reportada`**, e calcular
o resíduo `W_observado − W_previsto`.

Fazer isso **durante** um salto grande (10→30), não em regime permanente.

| Resultado | Leitura |
|---|---|
| Resíduo **identicamente zero** em todas as amostras, inclusive no transiente | `W` é função pura dos valores publicados — nada físico entra |
| Resíduo negativo e decrescente durante a subida | `W` acompanha os ímãs → H1 |

> Não normalizar "no olho" pela cadência: ela cai quando a resistência sobe, e isso
> confunde. Prever com a cadência **observada em cada amostra** elimina o problema.

#### 1.5.b Desaceleração da roda de inércia — a parte objetiva ★

Esta é a peça que fecha a prova, e ela é **física de verdade, já gravada no log**: o
`0x2AD2` da bike carrega velocidade no segundo pacote (`PLANO-MEGAGYM` §2.1) ✅. A taxa de
desaceleração do volante é proporcional ao torque de frenagem — ou seja, à posição real
dos ímãs.

Três corridas, partindo de velocidade alta para alongar a curva:

| # | Procedimento | Produz |
|---|---|---|
| C1 | Estabilizar em R=13, acelerar, **parar de pedalar** | curva de decaimento A |
| C2 | Idem em R=32 | curva B, bem mais íngreme |
| C3 | Estabilizar em R=13, acelerar e **comandar R=32 no mesmo instante em que para de pedalar** | ver abaixo |

| C3 mostra | Leitura |
|---|---|
| Decaimento começa como A e **vai se inclinando** até B ao longo de N segundos | N é o tempo de chegada física dos ímãs, medido **objetivamente** |
| Decaimento é B desde o primeiro instante | Não há atraso físico — H0 cai |

**O teste se autovalida.** Se o canal de velocidade da bike fosse sintético a partir da
potência, com cadência zero ele iria a zero de uma vez. Decaimento suave e contínuo prova
que a velocidade vem do volante — e portanto que a curva é física.

> **Roda-livre:** se a bike for de transmissão fixa, os pedais continuam girando; deixar as
> pernas serem levadas passivamente, sem aplicar torque, em vez de tirar os pés.
>
> **Amostragem:** o pacote com velocidade chega a cada ~2 s (os dois `0x2AD2` se alternam)
> ✅. Uma queda de 10 s rende ~5 pontos. Partir de velocidade alta e **repetir 3× cada
> corrida** para empilhar.

#### 1.5.c A conclusão

- **1.5.a** ⇒ `W` é função exata de `R_reportado` e `cadência_reportada`.
- **1.5.b** ⇒ `R_reportado ≠ R_físico` durante N segundos, objetivamente.
- Logo `W ≠ f(R_físico)`. **H1 cai, e sobra H0.** ∎

| Aceita se | |
|---|---|
| Resíduo da 1.5.a estatisticamente indistinguível de zero no transiente | ✅ |
| C3 mostra inclinação progressiva de A para B | ✅ |

**Artefatos:** log do QZ das três corridas · série de resíduos · as três curvas de
decaimento sobrepostas.

#### 1.5.d Corroboração dramática, e mede a §3.4.4 de quebra

Comandar R alternando **13 ↔ 32 a cada 2 s**, mais rápido do que o atuador consegue
seguir, pedalando de forma estável.

O watt publicado deve varrer a amplitude inteira — 111 ↔ 316 W a 80 rpm — em onda
quadrada, enquanto a perna sente uma variação bem menor e suavizada. Amplitude total no
número contra amplitude parcial na perna **é** o desacoplamento, visível a olho nu.

Isso também é a medição da `VIABILIDADE` §3.4.4: se o atuador não acompanha 2 s, não
acompanha a demanda do MyWhoosh a ~1005 ms de mediana.

#### 1.5.e O limite honesto

Isto prova que `W` é calculado sobre o alvo. **Não prova qual é o watt verdadeiro** — para
isso, e só para isso, é preciso pedal ou pedivela com extensômetro (`VIABILIDADE` §7.3).

---

### 1.6 Calibração da `ergTable` — só se quiser ERG

Pular se você só vai fazer free ride. Se quiser treino estruturado, é o `PLANO-MEGAGYM`
Fase 2 sem alterações: degraus **R = 5, 8, 11, 14, 17, 20, 23, 26, 29, 32**, ~2 min cada a
80 rpm ±2, depois a escada em ~65 rpm e em ~95 rpm com 1 min por nível.

> O atraso do atuador **não corrompe esta tabela.** A potência publicada sempre concorda
> com a resistência publicada, então a tabela aprendida é internamente consistente e a
> janela de descarte de 1000 ms (`src/ergtable.h:112`) é inofensiva aqui
> (`VIABILIDADE` §3.4.3) ⚙️. Ela só não descreve física.

| Aceita se | |
|---|---|
| Log tem `Added/Updated point` cobrindo os 10 níveis nas 3 cadências | ✅ |

**Artefatos:** log · **valor da chave `ergDataPoints`** extraído das settings — é o que
viaja para o Pi na Etapa 3, numa linha só (`VIABILIDADE` §2.2.1) ⚙️.

**Portão da Etapa 1:** 1.1, 1.2 e 1.5 aprovados. 1.3 e 1.4 executados e registrados.

---

## Etapa 2 — BikeControl no mesmo PC, por OpenBikeControl

**Objetivo:** marcha com semântica de verdade **sem tocar no caminho de dados**.

**Material:** nada — BikeControl da Microsoft Store (`9NP42GS03Z26`) ou do ZIP oficial.

### 2.1 Conectar por OBC/mDNS

O MyWhoosh suporta OBC **só por mDNS**, nunca por BLE (`my_whoosh.dart`,
`connections => [myWhooshLink, obpMdns]`) ⚙️. Escolher **OpenBikeControl (mDNS)** — não
"Link", não "Local".

Dois pré-requisitos que não são opcionais nesta configuração:

- **Desabilitar IPv6 no adaptador** (`ncpa.cpl` → Propriedades → desmarcar IPv6). O
  `INSTRUCTIONS_WINDOWS_IPV6.md` é escrito para exatamente "BikeControl e MyWhoosh no mesmo
  Windows" e diz que sem isso a conexão OBC não se estabelece ⚙️.
- **Firewall:** quem anuncia é o BikeControl e **quem abre o TCP é o MyWhoosh**
  (`MDNS.md`) ⚙️ — liberar entrada para o BikeControl e mDNS em UDP 5353, em rede Privada.

| Aceita se | |
|---|---|
| BikeControl mostra conexão OBC ativa | ✅ |
| Serviço `_openbikecontrol._tcp.local.` visível na rede | ✅ |
| MyWhoosh troca marcha ao acionar o controle | ✅ |

### 2.2 Índice absoluto de marcha ★

É o que diferencia OBC de simular tecla: o botão `0x03` **Gear Set** manda índice absoluto,
`0x02` = marcha 1 … até 254, sem escala (`PROTOCOL.md`) ⚙️. O MyWhoosh declara 30 marchas
(`virtualGearAmount => 30`) ⚙️.

**Teste:** 20 trocas aleatórias, **incluindo bater nas duas pontas** (tentar descer abaixo
de 1 e subir acima de 30) e depois voltar ao meio.

| Aceita se | |
|---|---|
| A marcha no BikeControl e a no MyWhoosh coincidem ao fim das 20 trocas | ✅ |
| Bater nas pontas **não** dessincroniza | ✅ |

Esse é o teste que uma solução por I/K reprovaria: tecla manda incremento relativo e o
estado deriva.

### 2.3 Caminho de dados inalterado ★★ — o critério crítico

**Repetir a 1.2 exatamente**, com o BikeControl rodando.

| Aceita se | |
|---|---|
| Médias de potência e cadência dentro do ruído da referência da 1.2 | ✅ |

**Se divergirem, o BikeControl entrou no caminho de dados e não deveria ter entrado.**
Nessa configuração a marcha vai por rede e o trainer vai por BLE do QZ direto ao MyWhoosh —
são caminhos independentes. Divergência significa que o modo proxy foi ativado por engano.

**Artefatos:** log do QZ · `.fit` · as três médias, lado a lado com as da 1.2.

### 2.4 Limite de duração ❓

Pedalar **> 25 min** com marcha ativa e observar se o virtual shifting para.

| Resultado | Consequência |
|---|---|
| Não para | O teto de 20 min/dia é só do virtual shifting próprio do BikeControl (modo proxy) |
| Para aos 20 min | Precisa de Pro, ou volte para teclado numérico USB (~R$ 50, sem limite) |

**Portão da Etapa 2:** 2.2 e 2.3 aprovados.

---

## Etapa 3 — Raspberry Pi, sem bridge

**Objetivo:** substituir o tablet pelo Pi e **medir a coexistência de rádio** — a medição
que decide se a Etapa 4 tem motivo para existir.

**Material** (`VIABILIDADE` §7.4):

| Item | ~R$ |
|---|---|
| Pi 4 Model B **1 GB** | 599 |
| Fonte USB-C **5,1 V / 3 A** de qualidade | 60–120 |
| MicroSD 32 GB A1 de marca | 40–70 |
| Case com cooler ou dissipador | 40–100 |
| RTC **DS3231** (I²C) | 15–25 |
| Adaptador **USB-TTL** (console serial) | 15–25 |
| **Total** | **~770–940** |

> A fonte é o item onde não economizar: subtensão no Pi 4 produz comportamento errático de
> USB e rádio — exatamente o ruído que envenenaria a medição 3.3.

### 3.1 Instalar e transferir a configuração

Binário: artefato `raspberry-pi-binary-aarch64` do run corrente do master
(`.github/workflows/main.yml:1525`) ⚙️ — dispensa as ~45 min de compilação.

Transferência da config: o `.qzs` é um INI de `QSettings` com os mesmos nomes de chave do
`qDomyos-Zwift.conf` do Pi (`homeform.cpp:11029`, `:11053`) ⚙️. Mesclar as chaves da Etapa
1 em `/root/.config/'Roberto Viola'/qDomyos-Zwift.conf`, **incluindo `ergDataPoints`**.

> **Pule chaves com `password` ou `token`** — são cifradas com `SimpleCrypt` no `.qzs` e o
> valor cifrado não serve no `.conf`. Configure-as direto no Pi, se precisar ⚙️.

**Relógio:** ativar o DS3231 e conferir que a hora sobrevive ao boot sem rede. Sem isso a
correlação log↔`.fit` por hora de parede — a ferramenta analítica do 1.2 — quebra.

| Aceita se | |
|---|---|
| QZ sobe, conecta na bike, e anuncia como **`QZPI`** (`virtualbike.cpp:84`, guardado por `Q_OS_LINUX`) ⚙️ | ✅ |
| MyWhoosh encontra e conecta no `QZPI` | ✅ |
| Hora correta após reboot com a rede desligada | ✅ |

### 3.2 Caminho de dados, terceira validação

**Repetir a 1.2 pela terceira vez**, agora com o Pi no lugar do tablet.

| Aceita se | |
|---|---|
| Médias dentro do ruído da referência da 1.2 | ✅ |

### 3.3 Coexistência de rádio ★★★ — a medição que decide a Etapa 4

**Preparo, e cada item importa:**

- **WiFi desligado** (`rfkill block wifi`). WiFi e BT dividem o mesmo chip combo e a mesma
  antena; com WiFi ativo, uma degradação **não distingue** contenção WiFi/BT de papel duplo
  BLE (`VIABILIDADE` §7.4).
- **Nenhum vínculo BLE extra no Pi** — sem cinta cardíaca, sem shifter, sem sensor. A
  medição isola *central + peripheral*; qualquer link a mais muda o que está sendo medido
  (`VIABILIDADE` §6.2.1). A marcha da Etapa 2 já vive no PC, por rede — não interfere.
- **No mínimo três sessões de ≥ 60 min.**

| Aceita se | |
|---|---|
| Nenhuma desconexão da bike nem do MyWhoosh na sessão inteira | ✅ |
| O virtualbike segue anunciando/conectado do início ao fim — sem o sintoma da [#1702](https://github.com/cagnulein/qdomyos-zwift/issues/1702) | ✅ |
| Sem lacunas em `0x2AD2` acima de ~5 s | ✅ |

| Resultado | Consequência |
|---|---|
| **Aprova** | O ganho nº 1 do bridge evapora, e com ele a motivação principal da Etapa 4 (`VIABILIDADE` §6.2.1) |
| **Reprova** | Antes da Etapa 4, testar a alternativa barata: prender o central em `hci1` com um dongle **RTL8761B** (não "CSR 4.0") — exige mudança pequena no QZ, `bluetooth.cpp:152` ⚙️ |

**Artefatos:** log do QZ · `journalctl` · `.fit` · hora de início e fim de cada sessão.

### 3.4 Integridade do log

| Aceita se | |
|---|---|
| Última linha do log a < 1 min do fechamento, sem corte em ~40,5 MB | ✅ |

Confirma (ou refuta) a hipótese de que o defeito §5.3 do `PLANO-MEGAGYM` era do
storage/SAF do Android (`VIABILIDADE` §2.4) ❓.

**Portão da Etapa 3:** 3.1, 3.2 e 3.4 aprovados. 3.3 registrado, aprovando ou não.

---

## Etapa 4 — Hardware: caracterizar antes de decidir

**Só faz sentido se** a 3.3 reprovou, **ou** se você quer os ganhos 2 e 3 da
`VIABILIDADE` §6.1 (os 12 níveis físicos escondidos e o estado real do atuador).

**Material:**

| Item | Para quê | ~R$ |
|---|---|---|
| **Multímetro com frequencímetro** | Mede AC e frequência, não só DC. Um DT-830B **não** basta | 80–120 |
| Protoboard, jumpers, garras jacaré | Medir sem cortar fio | 20–40 |
| Analisador lógico USB 8 ch | Só se aparecer barramento digital | 35–60 |
| ESP32 DevKit (WROOM-32) | Só na fase de protótipo | 35–60 |
| Transceptor **MAX3232** em módulo | Só se as tensões indicarem RS-232 | 10–20 |

> **Regra de segurança, sem exceção:** só medição passiva até entender. O chicote carrega
> AC de gerador, possivelmente trifásica (`VIABILIDADE` §4.1) ✅, e o precedente da Peloton
> mostra que barramento de console pode ser RS-232 de ±15 V ⚙️. Uma ponta de prova errada
> mata o console — a peça mais cara e, numa YPOO rebrandeada, provavelmente irreparável.

### 4.1 Caracterização do chicote

Já resolvido: **5 a 8 pinos** ✅ · **self-generating com retenção de energia** ✅.

| Ensaio | Decide |
|---|---|
| **Quais pinos carregam AC ao pedalar** ★ | É o discriminador único que resta entre eixo 2 (analógico) e eixo 3 (barramento). Identificada a dupla do gerador, o resto sai por contagem |
| A frequência dessa AC acompanha a cadência? | Se sim, existe leitura de cadência **totalmente passiva**, de dois fios |
| Tensão DC de repouso por pino | Separa rails de sinal |
| O que muda ao varrer R 1→32 | Tensão linear = pot/DAC · par que inverte = DC + ponte H · quatro fases = stepper · trem de bits = barramento |
| Foto da placa do console | L298/DRV88xx/TB6612 presente ⇒ ponte H no console |

### 4.2 Portão de decisão

Com 1.3, 1.4 e 4.1 na mão, a `VIABILIDADE` §6.2 já tem os números que faltavam para
decidir. Antes de qualquer corte, considerar o degrau da §6.2.2: **tap passivo, sem
injeção** — lê cadência e posição do atuador, mantém o controle por FTMS, risco elétrico
quase nulo, e prova o caminho serial dentro do QZ antes de algo irreversível.

---

## Resumo de artefatos

| Etapa | Log QZ | `.fit` MyWhoosh | Outros |
|---|---|---|---|
| 1.1 | ✔ | — | export `.qzs` |
| 1.2 ★ | ✔ | ✔ | **as três médias — referência do plano** |
| 1.3 ★ | ✔ | — | tabela de `t_reportado` e `t_sentido` |
| 1.4 | — | — | fotos do entreferro · tempos de motor |
| 1.5 ★ | ✔ | — | série de resíduos · 3 curvas de decaimento |
| 1.6 | ✔ | — | valor de `ergDataPoints` |
| 2.2 | — | — | log de marchas dos dois lados |
| 2.3 ★ | ✔ | ✔ | médias vs. 1.2 |
| 3.2 | ✔ | ✔ | médias vs. 1.2 |
| 3.3 ★ | ✔ | ✔ | `journalctl` · horários das sessões |
| 4.1 | — | — | tabela de pinos · fotos da placa |

## Orçamento por etapa

| Etapa | Gasto |
|---|---|
| 1 | **R$ 0** |
| 2 | R$ 0 (eventual assinatura Pro, decidida pela 2.4) |
| 3 | ~R$ 770–940 |
| 4 | ~R$ 100–160 para caracterizar; protótipo só depois do portão |

Fora do plano, e opcional em qualquer ponto: **medidor de potência de pedal ou pedivela**,
R$ 2.000–4.500 (`VIABILIDADE` §7.3). Não é pré-requisito de nenhuma etapa. É a única coisa
que tornaria falsificável qualquer afirmação sobre watt absoluto.
