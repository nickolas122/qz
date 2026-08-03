#!/usr/bin/env python3
"""
paddle_logger.py - Logger BLE/FTMS para caracterizar o comportamento dos paddles.

Objetivo (etapa 1 do plano de fork do QZ):
  descobrir, com timestamp, como a bike reporta mudanca de nivel de resistencia,
  quantos niveis cada toque de paddle produz, e qual a latencia da notificacao.

Uso:
    pip install bleak
    python paddle_logger.py --scan                 # lista dispositivos
    python paddle_logger.py --name MEGA            # conecta por trecho do nome
    python paddle_logger.py --addr AA:BB:CC:DD:EE:FF
    python paddle_logger.py --addr AA:BB:CC:DD:EE:FF --test-write 10
                                                   # tenta ESCREVER resistencia 10

Gera paddle_log.txt no diretorio atual, alem de imprimir na tela.

IMPORTANTE: a bike aceita UMA conexao por vez.
Feche QZ / Kinomap / MyWhoosh / BikeControl antes de rodar.
"""

import argparse
import asyncio
import time
from datetime import datetime

from bleak import BleakClient, BleakScanner

# --- UUIDs FTMS ---
SVC_FTMS = "00001826-0000-1000-8000-00805f9b34fb"
CHR_INDOOR_BIKE_DATA = "00002ad2-0000-1000-8000-00805f9b34fb"
CHR_MACHINE_STATUS = "00002ada-0000-1000-8000-00805f9b34fb"
CHR_MACHINE_FEATURE = "00002acc-0000-1000-8000-00805f9b34fb"
CHR_CONTROL_POINT = "00002ad9-0000-1000-8000-00805f9b34fb"
CHR_RESISTANCE_RANGE = "00002ad6-0000-1000-8000-00805f9b34fb"

# Opcodes do Control Point (espelham ftmsbike.h)
FTMS_REQUEST_CONTROL = 0x00
FTMS_SET_TARGET_RESISTANCE_LEVEL = 0x04
FTMS_START_RESUME = 0x07

T0 = time.monotonic()
LOGFILE = None


def ts() -> str:
    return f"{time.monotonic() - T0:9.3f}"


def emit(line: str) -> None:
    print(line)
    if LOGFILE:
        LOGFILE.write(line + "\n")
        LOGFILE.flush()


# ---------------------------------------------------------------- parsers

def parse_indoor_bike_data(data: bytes) -> dict:
    """Decodifica 0x2AD2. Campos sao condicionais ao bitfield de flags."""
    if len(data) < 2:
        return {}
    flags = int.from_bytes(data[0:2], "little")
    i = 2
    out = {}

    def take(n: int, signed: bool = False):
        nonlocal i
        if i + n > len(data):
            raise IndexError("truncado")
        v = int.from_bytes(data[i:i + n], "little", signed=signed)
        i += n
        return v

    try:
        # bit 0 tem logica INVERTIDA: 0 = velocidade instantanea presente
        if not (flags & 0x0001):
            out["speed"] = take(2) / 100.0
        if flags & 0x0002:
            out["avg_speed"] = take(2) / 100.0
        if flags & 0x0004:
            out["cadence"] = take(2) / 2.0
        if flags & 0x0008:
            out["avg_cadence"] = take(2) / 2.0
        if flags & 0x0010:
            out["distance"] = take(3)
        if flags & 0x0020:
            out["resistance"] = take(2, signed=True)
        if flags & 0x0040:
            out["power"] = take(2, signed=True)
        if flags & 0x0080:
            out["avg_power"] = take(2, signed=True)
        if flags & 0x0100:
            out["energy_total"] = take(2)
            out["energy_hour"] = take(2)
            out["energy_min"] = take(1)
        if flags & 0x0200:
            out["hr"] = take(1)
        if flags & 0x0400:
            out["met"] = take(1)
        if flags & 0x0800:
            out["elapsed"] = take(2)
        if flags & 0x1000:
            out["remaining"] = take(2)
    except IndexError:
        out["_truncado"] = True

    out["_flags"] = f"0x{flags:04X}"
    return out


STATUS_OPCODES = {
    0x01: "Reset",
    0x02: "Parado/pausado pelo usuario",
    0x03: "Parado por safety key",
    0x04: "Iniciado ou retomado",
    0x05: "Target Speed alterado",
    0x06: "Target Incline alterado",
    0x07: "RESISTANCE LEVEL ALTERADO",
    0x08: "Target Power alterado",
    0x09: "Target HR alterado",
    0x12: "Indoor Bike Simulation Params alterado",
    0xFF: "Control Permission Lost",
}

# Codigos de resultado da resposta do Control Point (0x80 <opcode> <resultado>)
CP_RESULT_CODES = {
    0x01: "Success",
    0x02: "Op Code not supported",
    0x03: "Invalid Parameter",
    0x04: "Operation Failed",
    0x05: "Control Not Permitted",
}


def parse_control_point_response(data: bytes) -> str:
    b = bytes(data)
    if len(b) >= 3 and b[0] == 0x80:
        res = CP_RESULT_CODES.get(b[2], f"resultado desconhecido 0x{b[2]:02X}")
        return f"resposta ao opcode 0x{b[1]:02X} -> {res}"
    return "formato inesperado"


def parse_machine_status(data: bytes) -> str:
    if not data:
        return "vazio"
    op = data[0]
    name = STATUS_OPCODES.get(op, f"opcode desconhecido 0x{op:02X}")
    payload = data[1:]
    extra = ""
    if op == 0x07 and payload:
        # normalmente uint8 ou sint16, dependendo do firmware
        if len(payload) == 1:
            extra = f" -> nivel={payload[0]}"
        else:
            extra = (f" -> nivel_u8={payload[0]} "
                     f"nivel_s16={int.from_bytes(payload[0:2], 'little', signed=True)}")
    return f"{name}{extra}"


# ---------------------------------------------------------------- estado

class Tracker:
    """Detecta mudancas de nivel e mede o intervalo entre elas."""

    def __init__(self):
        self.last_level = None
        self.last_change_t = None

    def feed(self, level: int, origem: str) -> None:
        if level is None:
            return
        if self.last_level is None:
            self.last_level = level
            emit(f"[{ts()}] BASELINE nivel={level} (via {origem})")
            return
        if level != self.last_level:
            delta = level - self.last_level
            now = time.monotonic()
            gap = ""
            if self.last_change_t is not None:
                gap = f" | {(now - self.last_change_t) * 1000:.0f} ms desde a anterior"
            emit(f"[{ts()}] >>> MUDANCA {self.last_level} -> {level} "
                 f"(delta={delta:+d}) via {origem}{gap}")
            self.last_level = level
            self.last_change_t = now


# ---------------------------------------------------------------- teste de escrita

async def test_write(client, level: int, estado: dict) -> None:
    """Escreve no 0x2AD9 e loga as respostas (indicate) e o STAT 07 resultante.

    Testa os DOIS formatos do parametro de resistencia:
      A) nivel x 10, little-endian  -> o que o QZ manda em ftmsbike.cpp:374-376
      B) nivel inteiro puro         -> hipotese, ja que a bike REPORTA uint8 puro

    ESPERA a bike estar pedalando antes de escrever: na rodada de 2026-08-01 a
    bike respondeu Success ao formato B mas NAO mudou a resistencia, e estava
    parada (cadence=0) o teste inteiro - resultado inconclusivo.
    """
    respostas: "asyncio.Queue[bytes]" = asyncio.Queue()

    def on_cp(_, data: bytearray):
        b = bytes(data)
        emit(f"[{ts()}] CP<< {b.hex('-')}  | {parse_control_point_response(b)}")
        respostas.put_nowait(b)

    try:
        await client.start_notify(CHR_CONTROL_POINT, on_cp)
        emit(f"[{ts()}] indicate ON: 0x2AD9 (Control Point)")
    except Exception as e:
        emit(f"[{ts()}] 0x2AD9 indisponivel para indicate: {e} - abortando teste")
        return

    async def escreve(payload: bytes, descricao: str) -> bool:
        while not respostas.empty():
            respostas.get_nowait()
        emit(f"[{ts()}] CP>> {payload.hex('-')}  | {descricao}")
        try:
            await client.write_gatt_char(CHR_CONTROL_POINT, payload, response=True)
        except Exception as e:
            emit(f"[{ts()}] falha na escrita: {e}")
            return False
        try:
            resp = await asyncio.wait_for(respostas.get(), timeout=3.0)
        except asyncio.TimeoutError:
            emit(f"[{ts()}] SEM RESPOSTA em 3s")
            return False
        return len(resp) >= 3 and resp[0] == 0x80 and resp[2] == 0x01

    emit("\n=== TESTE DE ESCRITA NO CONTROL POINT ===")
    emit(">>> COMECE A PEDALAR. O teste so escreve com a bike em movimento. <<<")

    espera_ate = time.monotonic() + 60.0
    while estado.get("cadence", 0) <= 0:
        if time.monotonic() > espera_ate:
            emit(f"[{ts()}] 60s sem cadencia - escrevendo com a bike PARADA "
                 f"(resultado sera inconclusivo se nada mudar)")
            break
        await asyncio.sleep(0.5)
    else:
        emit(f"[{ts()}] cadencia detectada ({estado['cadence']} rpm) - iniciando")

    ok = await escreve(bytes([FTMS_REQUEST_CONTROL]), "Request Control")
    if not ok:
        emit(f"[{ts()}] Request Control falhou - os testes abaixo provavelmente "
             f"vao dar 'Control Not Permitted'")

    # O QZ faz REQUEST_CONTROL -> START_RESUME (ftmsbike.cpp:171-195)
    await escreve(bytes([FTMS_START_RESUME]), "Start/Resume")

    for descricao, valor in (
        (f"Set Target Resistance FORMATO A (nivel {level} x 10)", level * 10),
        (f"Set Target Resistance FORMATO B (nivel {level} puro)", level),
    ):
        antes = estado.get("resistance")
        payload = bytes([FTMS_SET_TARGET_RESISTANCE_LEVEL,
                         valor & 0xFF, (valor >> 8) & 0xFF])
        aceito = await escreve(payload, descricao)
        emit(f"[{ts()}] -> ACK {'ACEITO' if aceito else 'RECUSADO/sem resposta'}; "
             f"resistencia antes={antes}; aguardando 5s (pedale!)")
        await asyncio.sleep(5.0)
        depois = estado.get("resistance")
        if depois == level:
            veredito = f"APLICOU: resistencia chegou em {depois} (= solicitado)"
        elif depois != antes:
            veredito = (f"MUDOU mas nao bate: {antes} -> {depois}, "
                        f"solicitado {level} (checar escala)")
        else:
            veredito = (f"NAO APLICOU: resistencia continua {depois} "
                        f"{'apesar do ACK de sucesso' if aceito else ''}")
        emit(f"[{ts()}] === VEREDITO {descricao}: {veredito}")

    emit(f"=== FIM DO TESTE (solicitado nivel {level}). ===\n")

    try:
        await client.stop_notify(CHR_CONTROL_POINT)
    except Exception:
        pass


# ---------------------------------------------------------------- main

async def run(address: str, write_level: int = None) -> None:
    tracker = Tracker()
    # ultimo IBD visto; o teste de escrita le daqui pra saber se aplicou
    estado = {}

    emit(f"# sessao iniciada {datetime.now().isoformat(timespec='seconds')}")
    emit(f"# alvo: {address}")

    async with BleakClient(address) as client:
        emit(f"[{ts()}] conectado")

        # Inventario de caracteristicas
        emit("\n--- caracteristicas do servico FTMS ---")
        for svc in client.services:
            if svc.uuid.lower() != SVC_FTMS:
                continue
            for ch in svc.characteristics:
                emit(f"  {ch.uuid}  props={','.join(ch.properties)}")
        emit("---\n")

        # Le a feature uma vez, pra deixar registrado no log
        try:
            feat = await client.read_gatt_char(CHR_MACHINE_FEATURE)
            emit(f"[{ts()}] 0x2ACC raw = {feat.hex('-')}")
            machine = int.from_bytes(feat[0:4], "little")
            target = int.from_bytes(feat[4:8], "little")
            emit(f"[{ts()}] machine_features=0x{machine:08X} "
                 f"target_features=0x{target:08X}")
            emit(f"[{ts()}] bit2  (Resistance Target)      = "
                 f"{'SIM' if target & (1 << 2) else 'NAO'}")
            emit(f"[{ts()}] bit3  (Power Target / ERG)     = "
                 f"{'SIM' if target & (1 << 3) else 'NAO'}")
            emit(f"[{ts()}] bit13 (Indoor Bike Simulation) = "
                 f"{'SIM' if target & (1 << 13) else 'NAO'}")
        except Exception as e:
            emit(f"[{ts()}] falha lendo 0x2ACC: {e}")

        # 0x2AD6 - Supported Resistance Level Range: 3 x sint16 (min, max, incremento)
        try:
            rng = await client.read_gatt_char(CHR_RESISTANCE_RANGE)
            emit(f"[{ts()}] 0x2AD6 raw = {rng.hex('-')}")
            if len(rng) >= 6:
                mn = int.from_bytes(rng[0:2], "little", signed=True)
                mx = int.from_bytes(rng[2:4], "little", signed=True)
                inc = int.from_bytes(rng[4:6], "little", signed=True)
                emit(f"[{ts()}] resistencia: min={mn} max={mx} incremento={inc} "
                     f"(bruto, sem aplicar resolucao)")
                emit(f"[{ts()}] se resolucao 0.1 -> min={mn / 10} max={mx / 10} "
                     f"incremento={inc / 10}")
            else:
                emit(f"[{ts()}] 0x2AD6 curto demais ({len(rng)} bytes)")
        except Exception as e:
            emit(f"[{ts()}] falha lendo 0x2AD6: {e}")

        def on_ibd(_, data: bytearray):
            d = parse_indoor_bike_data(bytes(data))
            raw = bytes(data).hex("-")
            campos = " ".join(f"{k}={v}" for k, v in d.items()
                              if not k.startswith("_"))
            emit(f"[{ts()}] IBD  {raw}  | {campos}")
            # os dois pacotes que a bike alterna carregam campos diferentes;
            # so atualiza o que veio, senao um zera o outro
            for k in ("cadence", "resistance"):
                if k in d:
                    estado[k] = d[k]
            tracker.feed(d.get("resistance"), "0x2AD2")

        def on_status(_, data: bytearray):
            raw = bytes(data).hex("-")
            emit(f"[{ts()}] STAT {raw}  | {parse_machine_status(bytes(data))}")
            b = bytes(data)
            if b and b[0] == 0x07 and len(b) >= 2:
                tracker.feed(b[1], "0x2ADA")

        started = []
        try:
            await client.start_notify(CHR_INDOOR_BIKE_DATA, on_ibd)
            started.append(CHR_INDOOR_BIKE_DATA)
            emit(f"[{ts()}] notify ON: 0x2AD2 (Indoor Bike Data)")
        except Exception as e:
            emit(f"[{ts()}] 0x2AD2 indisponivel: {e}")

        try:
            await client.start_notify(CHR_MACHINE_STATUS, on_status)
            started.append(CHR_MACHINE_STATUS)
            emit(f"[{ts()}] notify ON: 0x2ADA (Machine Status)")
        except Exception as e:
            emit(f"[{ts()}] 0x2ADA indisponivel: {e}")

        if not started:
            emit("nenhuma notificacao disponivel - abortando")
            return

        if write_level is not None:
            await test_write(client, write_level, estado)

        emit("\n=== PEDALE E APERTE OS PADDLES. Ctrl+C para encerrar. ===")
        emit("=== sugestao: 1 toque, espere 3s, 1 toque, espere 3s,")
        emit("===           depois 3 toques rapidos seguidos.\n")

        try:
            while client.is_connected:
                await asyncio.sleep(1)
        except asyncio.CancelledError:
            pass
        finally:
            for u in started:
                try:
                    await client.stop_notify(u)
                except Exception:
                    pass
            emit(f"[{ts()}] encerrado")


async def scan() -> None:
    print("escaneando 8s...")
    devs = await BleakScanner.discover(timeout=8.0, return_adv=True)
    for d, adv in devs.values():
        print(f"  {d.address}  rssi={adv.rssi:>4}  {d.name or '(sem nome)'}")


async def find_by_name(fragment: str) -> str:
    print(f"procurando por '{fragment}'...")
    devs = await BleakScanner.discover(timeout=8.0, return_adv=True)
    frag = fragment.lower()
    for d, _adv in devs.values():
        if d.name and frag in d.name.lower():
            print(f"encontrado: {d.name} @ {d.address}")
            return d.address
    raise SystemExit("nao encontrado - rode com --scan para ver os nomes")


def main() -> None:
    global LOGFILE
    ap = argparse.ArgumentParser()
    ap.add_argument("--scan", action="store_true", help="apenas lista dispositivos")
    ap.add_argument("--addr", help="endereco MAC / UUID do dispositivo")
    ap.add_argument("--name", help="trecho do nome do dispositivo")
    ap.add_argument("--out", default="paddle_log.txt", help="arquivo de log")
    ap.add_argument("--test-write", type=int, metavar="N",
                    help="apos conectar, tenta ajustar a resistencia para o nivel N "
                         "pelo Control Point (0x2AD9), nos dois formatos possiveis")
    args = ap.parse_args()

    if args.scan:
        asyncio.run(scan())
        return

    LOGFILE = open(args.out, "a", encoding="utf-8")

    if args.addr:
        addr = args.addr
    elif args.name:
        addr = asyncio.run(find_by_name(args.name))
    else:
        raise SystemExit("use --scan, --addr ou --name")

    try:
        asyncio.run(run(addr, args.test_write))
    except KeyboardInterrupt:
        emit("\ninterrompido pelo usuario")


if __name__ == "__main__":
    main()
