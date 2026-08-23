#!/usr/bin/env python3
"""Smoke test for the QZWS WebSocket - the one thing a PC can read a ride through.

QZ on a tablet with the training app on a PC is the arrangement that works (see
docs/fork/STRIP-SPEC.md 3.2.1), and this socket is how the PC sees the ride:
`tools/qz-rouvy-rtss/` draws gears, ERG and resistance from it, and
`tools/xbox-mywhoosh-gears/` shifts back through it. Both parse the wire format, so the
format is an interface - this checks it still holds after a change to
`templateinfosenderbuilder.cpp` or `webserverinfosender.cpp`.

Three things are asserted, in the order the two tools need them:

  1. the periodic `workout` broadcast arrives, and carries gears, resistance and
     autoresistance                                     (Route A of qz-rouvy-rtss)
  2. `getsettings` answers for a named key               (the ERG poll, same tool)
  3. `gears_plus` and `gears_minus` move the gear        (Route B of xbox-mywhoosh-gears)

Check 3 is the one that regressed silently before: the control signals were connected in
`homeform` from the inner endpoint only, so a shift arriving here was parsed, dispatched,
emitted and then dropped.

**QZ must be running with its UI.** The template manager is constructed alongside the
QML engine (`src/main.cpp`), so `-no-gui` has no QZWS at all. It was built in the
homeform constructor until phase 7c-2a moved it out, which is the only reason deleting
that class in 7c-2b did not take this socket with it. And the endpoint must be switched
on - which since 7c-2b has no UI either, so set `template_user_QZWS_enabled` and
`template_user_QZWS_port` (6666) directly in QSettings until phase 6 gives them a
control.

    python tools/qzws_smoke.py                       # localhost:6666
    python tools/qzws_smoke.py --host 192.168.1.50   # QZ on the tablet
    python tools/qzws_smoke.py --no-shift            # read-only, leaves the gear alone

Standard library only, like its neighbour in xbox-mywhoosh-gears - no websocket-client.
Not wired into CI: it needs a running GUI app, which is the kind of orchestration
docs/fork/VIRTUAL-BIKE.md keeps out of the pipeline. Run it by hand after touching the
template code, and before believing a release.
"""

import argparse
import base64
import json
import os
import socket
import struct
import sys
import time

failures = []


def check(ok, what, detail=""):
    print("  %-5s %s%s" % ("ok" if ok else "FAIL", what, ("  -- " + detail) if detail else ""))
    if not ok:
        failures.append(what)
    return ok


class WebSocket:
    """The smallest client that can both send and read text frames."""

    def __init__(self, host, port, timeout=5.0):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.settimeout(timeout)
        self.buf = b""
        key = base64.b64encode(os.urandom(16)).decode()
        req = (
            "GET / HTTP/1.1\r\n"
            "Host: %s:%d\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: %s\r\n"
            "Sec-WebSocket-Version: 13\r\n\r\n" % (host, port, key)
        )
        self.sock.sendall(req.encode())
        while b"\r\n\r\n" not in self.buf:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise IOError("server closed during the handshake")
            self.buf += chunk
        head, self.buf = self.buf.split(b"\r\n\r\n", 1)
        status = head.split(b"\r\n")[0]
        if b"101" not in status:
            raise IOError("handshake refused: %s" % status.decode(errors="replace"))

    def send(self, text):
        payload = text.encode()
        header = bytearray([0x81])
        n = len(payload)
        if n < 126:
            header.append(0x80 | n)
        elif n < (1 << 16):
            header.append(0x80 | 126)
            header += struct.pack(">H", n)
        else:
            header.append(0x80 | 127)
            header += struct.pack(">Q", n)
        mask = os.urandom(4)
        header += mask
        masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        self.sock.sendall(bytes(header) + masked)

    def _need(self, n):
        while len(self.buf) < n:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise IOError("server closed")
            self.buf += chunk

    def recv(self):
        """Return the next text frame's payload, skipping control and binary frames."""
        while True:
            self._need(2)
            b0, b1 = self.buf[0], self.buf[1]
            opcode = b0 & 0x0F
            n = b1 & 0x7F
            offset = 2
            if n == 126:
                self._need(4)
                n = struct.unpack(">H", self.buf[2:4])[0]
                offset = 4
            elif n == 127:
                self._need(10)
                n = struct.unpack(">Q", self.buf[2:10])[0]
                offset = 10
            if b1 & 0x80:            # a server should not mask, but do not assume it
                self._need(offset + 4)
                mask = self.buf[offset:offset + 4]
                offset += 4
            else:
                mask = None
            self._need(offset + n)
            body = self.buf[offset:offset + n]
            self.buf = self.buf[offset + n:]
            if mask:
                body = bytes(c ^ mask[i % 4] for i, c in enumerate(body))
            if opcode == 0x8:
                raise IOError("server sent a close frame")
            if opcode == 0x1:
                return body.decode(errors="replace")
            # 0x9/0xA ping-pong and 0x2 binary are not part of this protocol


    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def read_until(ws, msg, deadline):
    """Read frames until one carries `msg`, or the deadline passes."""
    while time.time() < deadline:
        ws.sock.settimeout(max(0.2, deadline - time.time()))
        try:
            frame = ws.recv()
        except socket.timeout:
            return None
        try:
            obj = json.loads(frame)
        except ValueError:
            continue
        if obj.get("msg") == msg:
            return obj
    return None


def content_of(obj):
    content = obj.get("content") if obj else None
    return content if isinstance(content, dict) else {}


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="127.0.0.1", help="host QZ runs on (default 127.0.0.1)")
    parser.add_argument("--port", type=int, default=6666, help="user_QZWS port (default 6666)")
    parser.add_argument("--timeout", type=float, default=15.0,
                        help="seconds to wait for each expected frame (default 15)")
    parser.add_argument("--no-shift", action="store_true",
                        help="skip the gears_plus/gears_minus check and leave the gear alone")
    args = parser.parse_args()

    print("QZWS smoke test against %s:%d" % (args.host, args.port))
    try:
        ws = WebSocket(args.host, args.port, timeout=args.timeout)
    except (OSError, IOError) as exc:
        check(False, "connect", str(exc))
        print("\nIs QZ running with its UI, and is user_QZWS enabled on this port?")
        return 1
    check(True, "connect")

    try:
        # 1. the broadcast qz-rouvy-rtss reads
        first = read_until(ws, "workout", time.time() + args.timeout)
        if not check(first is not None, "workout broadcast arrives",
                     "" if first else "nothing in %.0fs" % args.timeout):
            return 1
        content = content_of(first)
        for field in ("gears", "resistance", "autoresistance"):
            check(field in content, "broadcast carries %s" % field,
                  "" if field in content else "absent")
        gear = content.get("gears")
        print("       gears=%s resistance=%s autoresistance=%s"
              % (gear, content.get("resistance"), content.get("autoresistance")))

        # 2. the ERG poll the same tool makes every two seconds
        ws.send(json.dumps({"msg": "getsettings", "content": {"keys": ["zwift_erg"]}}))
        reply = read_until(ws, "R_getsettings", time.time() + args.timeout)
        if check(reply is not None, "getsettings answers",
                 "" if reply else "no R_getsettings in %.0fs" % args.timeout):
            check("zwift_erg" in content_of(reply), "getsettings returns the key asked for",
                  "got %s" % sorted(content_of(reply))[:4])

        # 3. the shift xbox-mywhoosh-gears sends
        if args.no_shift:
            print("  skip  gears_plus/gears_minus (--no-shift)")
        elif not isinstance(gear, (int, float)):
            check(False, "gears_plus moves the gear", "no numeric gear to compare against")
        else:
            shifted = None
            for msg in ("gears_plus", "gears_minus"):
                ws.send(json.dumps({"msg": msg}))
                check(read_until(ws, "R_" + msg, time.time() + args.timeout) is not None,
                      "%s acknowledged" % msg)
                # the first broadcast after the ack carries the new gear
                now = content_of(read_until(ws, "workout", time.time() + args.timeout)).get("gears")
                if msg == "gears_plus":
                    shifted = now
                    check(isinstance(now, (int, float)) and now > gear,
                          "gears_plus moves the gear", "%s -> %s" % (gear, now))
                else:
                    check(isinstance(now, (int, float)) and shifted is not None and now < shifted,
                          "gears_minus puts it back", "%s -> %s" % (shifted, now))
    except (OSError, IOError) as exc:
        check(False, "session", str(exc))
    finally:
        ws.close()

    print()
    if failures:
        print("FAILED: %s" % ", ".join(failures))
        return 1
    print("all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
