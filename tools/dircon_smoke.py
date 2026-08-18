#!/usr/bin/env python3
"""End-to-end smoke test: find the shipped QZ over mDNS and consume it over DIRCON.

Layer C's two-process check, from docs/fork/VIRTUAL-BIKE.md. The gtest suite proves the
same protocol in-process; this proves the *shipped executable* serves what the suite says
it serves, and it does the finding with a completely foreign mDNS stack - python's
`zeroconf` - rather than the `qmdnsengine` QZ announces with.

It is deliberately not wired into CI. Process orchestration is where flakiness comes from,
and this is the least load-bearing piece of the plan; run it by hand after touching DIRCON
or mDNS, and before believing a release.

    # launch QZ, discover it, consume it, then check the goodbye on exit
    python tools/dircon_smoke.py --binary C:/qz-gamepad/qdomyos-zwift.exe \
                                --ride tst/fixtures/rides/steady.ride

    # against something already running, or an Android emulator behind adb forward
    python tools/dircon_smoke.py --host 127.0.0.1 --port 36866

Needs `pip install zeroconf` for the discovery half. With --host/--port it skips discovery
and needs nothing but the standard library.
"""

import argparse
import socket
import struct
import subprocess
import sys
import time

SERVICE_TYPE = "_wahoo-fitness-tnp._tcp.local."

FITNESS_MACHINE = 0x1826
INDOOR_BIKE_DATA = 0x2AD2

failures = []


def check(ok, what, detail=""):
    print("  %-5s %s%s" % ("ok" if ok else "FAIL", what, ("  -- " + detail) if detail else ""))
    if not ok:
        failures.append(what)
    return ok


# --------------------------------------------------------------------------------------
# DIRCON, hand-rolled from dirconpacket.h - six-byte header, then a 128-bit UUID
# --------------------------------------------------------------------------------------


def uuid128(u):
    return bytes.fromhex("0000%04x00001000800000805f9b34fb" % u)


class Dircon:
    def __init__(self, host, port, timeout=5):
        self.sock = socket.create_connection((host, port), timeout)
        self.sock.settimeout(0.2)
        self.buf = b""
        self.seq = 1

    def close(self):
        self.sock.close()

    def _frames(self):
        out = []
        while len(self.buf) >= 6:
            length = (self.buf[4] << 8) | self.buf[5]
            if len(self.buf) < 6 + length:
                break
            out.append(self.buf[: 6 + length])
            self.buf = self.buf[6 + length :]
        return out

    def _pump(self):
        try:
            data = self.sock.recv(65536)
            if data:
                self.buf += data
        except socket.timeout:
            pass

    def request(self, ident, payload=b"", wait=3.0):
        """Send a request; return the frame that answers it, skipping notifications."""
        seq = self.seq
        self.seq = (self.seq + 1) & 0xFF
        self.sock.sendall(bytes([1, ident, seq, 0]) + struct.pack(">H", len(payload)) + payload)
        deadline = time.time() + wait
        while time.time() < deadline:
            self._pump()
            for frame in self._frames():
                if frame[1] != 0x06:
                    return frame
                self.notifications.append(frame)
        return None

    notifications = []

    def collect(self, uuid, seconds):
        """Every notification for `uuid` seen in the next `seconds`, decoded."""
        self.notifications = []
        out = []
        deadline = time.time() + seconds
        while time.time() < deadline:
            self._pump()
            for frame in self._frames():
                if frame[1] != 0x06:
                    continue
                if ((frame[8] << 8) | frame[9]) != uuid:
                    continue
                out.append(decode_indoor_bike_data(frame[6 + 16 :]))
        return out


def decode_indoor_bike_data(body):
    if len(body) < 12:
        return None
    return {
        "flags": body[0] | (body[1] << 8),
        "speed": (body[2] | (body[3] << 8)) / 100.0,
        "cadence": (body[4] | (body[5] << 8)) / 2.0,
        "resistance": body[6] | (body[7] << 8),
        "power": body[8] | (body[9] << 8),
        "heart": body[10],
    }


# --------------------------------------------------------------------------------------
# Discovery, with a foreign mDNS stack
# --------------------------------------------------------------------------------------


def discover(timeout):
    try:
        from zeroconf import ServiceBrowser, ServiceListener, Zeroconf
    except ImportError:
        print("zeroconf is not installed: pip install zeroconf, or pass --host/--port")
        sys.exit(2)

    found = {}

    class Listener(ServiceListener):
        def add_service(self, zc, type_, name):
            info = zc.get_service_info(type_, name, timeout=3000)
            if info:
                found[name] = info

        def update_service(self, zc, type_, name):
            self.add_service(zc, type_, name)

        def remove_service(self, zc, type_, name):
            found.pop(name, None)

    zc = Zeroconf()
    browser = ServiceBrowser(zc, SERVICE_TYPE, Listener())
    deadline = time.time() + timeout
    while time.time() < deadline and not found:
        time.sleep(0.25)
    return zc, browser, found


def wait_until_gone(zc, browser, name, timeout):
    """A goodbye removes the record. Poll zeroconf's own view of it rather than ours."""
    from zeroconf import ServiceBrowser  # noqa: F401  (kept for symmetry with discover)

    deadline = time.time() + timeout
    while time.time() < deadline:
        info = zc.get_service_info(SERVICE_TYPE, name, timeout=500)
        if info is None:
            return True
        time.sleep(0.5)
    return False


# --------------------------------------------------------------------------------------


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", help="qdomyos-zwift to launch; otherwise use what is running")
    parser.add_argument("--ride", help=".ride file for -simulated-bike")
    parser.add_argument("--host", help="skip discovery and connect here")
    parser.add_argument("--port", type=int, help="skip discovery and connect here")
    parser.add_argument("--timeout", type=float, default=25.0, help="discovery timeout, seconds")
    parser.add_argument("--check-goodbye", action="store_true",
                        help="also check the record is withdrawn on exit; needs a graceful "
                             "quit, which killing the process is not - see the code")
    args = parser.parse_args()

    process = None
    if args.binary:
        cmd = [args.binary, "-no-gui", "-no-virtual-device-bluetooth"]
        if args.ride:
            cmd += ["-simulated-bike", "-ride", args.ride]
        print("launching %s" % " ".join(cmd))
        process = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(6)

    zc = browser = None
    name = None
    try:
        if args.host and args.port:
            host, port = args.host, args.port
            print("\nskipping discovery, using %s:%d" % (host, port))
        else:
            print("\nDISCOVERY (zeroconf)")
            zc, browser, found = discover(args.timeout)
            if not check(bool(found), "the service is announced and resolvable",
                         "" if found else "nothing answered a browse for " + SERVICE_TYPE):
                return 1
            name, info = next(iter(found.items()))
            addresses = [socket.inet_ntoa(a) for a in info.addresses]
            props = {k.decode(): (v or b"").decode() for k, v in info.properties.items()}

            print("        %s" % name)
            print("        server=%s port=%d addresses=%s" % (info.server, info.port, addresses))
            print("        txt=%s" % props)

            check(" " not in info.server, "the SRV target is a legal hostname", info.server)
            check(bool(addresses), "the A record resolves to an address")
            check(all(a != "0.0.0.0" for a in addresses), "the address is not null", str(addresses))
            for key in ("mac-address", "serial-number", "ble-service-uuids"):
                check(key in props and props[key] != "", "TXT carries " + key)

            host, port = addresses[0], info.port

        print("\nDIRCON")
        client = Dircon(host, port)
        check(True, "connected to %s:%d" % (host, port))

        reply = client.request(0x01)
        services = []
        if reply and reply[3] == 0:
            body = reply[6:]
            services = [(body[i + 2] << 8) | body[i + 3] for i in range(0, len(body), 16)]
        check(FITNESS_MACHINE in services, "DISCOVER_SERVICES lists 0x1826",
              ", ".join("0x%04x" % s for s in services) or "no answer")

        reply = client.request(0x02, uuid128(FITNESS_MACHINE))
        chars = []
        if reply and reply[3] == 0:
            body = reply[6 + 16 :]
            chars = [(body[i + 2] << 8) | body[i + 3] for i in range(0, len(body), 17)]
        check(INDOOR_BIKE_DATA in chars, "0x1826 offers 0x2AD2",
              ", ".join("0x%04x" % c for c in chars) or "no answer")

        reply = client.request(0x03, uuid128(0x2ACC))
        feature = reply[6 + 16 :].hex() if reply and reply[3] == 0 else ""
        check(feature == "835400000ce00000", "0x2ACC is the expected feature word", feature)

        reply = client.request(0x04, uuid128(0x2AD9) + b"\x00")
        ack = reply[6 + 16 :].hex() if reply and reply[3] == 0 else ""
        check(ack == "800001", "REQUEST_CONTROL is acknowledged", ack)

        client.request(0x05, uuid128(INDOOR_BIKE_DATA) + b"\x01")
        frames = client.collect(INDOOR_BIKE_DATA, 4.0)
        check(len(frames) >= 2, "the 0x2AD2 stream is flowing", "%d frames in 4 s" % len(frames))
        if frames:
            last = frames[-1]
            print("        last frame: %s" % last)
            check(last["flags"] == 0x0264, "flags are 0x0264", "0x%04x" % last["flags"])
            check(last["power"] > 0, "power is non-zero", str(last["power"]))
            check(last["cadence"] > 0, "cadence is non-zero", str(last["cadence"]))
        client.close()

        if args.check_goodbye and process and name and zc:
            # Only on request, because a kill is not a quit. `terminate()` is
            # TerminateProcess on Windows and SIGTERM on Linux, and neither unwinds the Qt
            # event loop, so `aboutToQuit` never fires and no goodbye is sent - the check
            # would fail for a reason that has nothing to do with the code. The goodbye is
            # covered properly, and revert-checked, by DirconDiscovery in the gtest suite;
            # this is here for the case where you can quit QZ by hand and want to watch a
            # foreign browser notice.
            print("\nGOODBYE")
            process.terminate()
            process.wait(timeout=15)
            process = None
            check(wait_until_gone(zc, browser, name, 15),
                  "the record is withdrawn when QZ exits",
                  "a client that caches this would keep a dead endpoint")

    finally:
        if zc:
            zc.close()
        if process:
            process.terminate()

    print("")
    if failures:
        print("FAILED: %d check(s): %s" % (len(failures), "; ".join(failures)))
        return 1
    print("all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
