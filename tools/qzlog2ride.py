#!/usr/bin/env python3
"""Turn a QZ debug log into a replayable fixture, and an oracle for what QZ wrote back.

Phase 5 of docs/fork/VIRTUAL-BIKE.md. QZ already logs every FTMS frame in both directions,
so every debug log from a real ride is a recording of that ride in this fork's own format.
This extracts one into something a harness can replay, which is the only way to get fixtures
that contain the bike's actual quirks - the flag combinations it really sets, the fields it
really omits, the byte it sends that no field accounts for. None of that is inventable at a
desk, and a hand-written fixture can only ever contain frames someone already believed in.

    # record a ride
    python tools/qzlog2ride.py debug-Sun_Aug_16_20_12_53_2026.log -o ride.frames

    # check the recording reproduces the metrics QZ derived at the time
    python tools/qzlog2ride.py --verify ride.frames

    # what QZ wrote back, on its own, for diffing against a later run
    python tools/qzlog2ride.py --oracle ride.frames

## What a log gives, and what it does not

`<<` lines carry the timestamp, the characteristic UUID, the declared length and the bytes.
`>>` lines carry the timestamp, the bytes and QZ's own description of the write - but *not*
the UUID, because `ftmsbike.cpp:174` does not log it. Writes are recorded with the UUID left
as `?` rather than guessed at; see docs/fork/TODO.md.

Immediately after each `<<` frame QZ logs the metrics it derived from it ("Current Watt: 0"
and friends). Those are captured with the frame, which is what makes `--verify` possible and
what gives Layer B something to assert against.
"""

import argparse
import re
import sys

# "{00002ad2-0000-1000-8000-00805f9b34fb}" 18 " << " "f5 01 00 ..."
NOTIFY = re.compile(
    r'"\{0000([0-9a-fA-F]{4})-[0-9a-fA-F-]+\}"\s+(\d+)\s+" << "\s+"([0-9a-fA-F ]*)"')
# " >> 04 8c 00 // forceResistance 14"
WRITE = re.compile(r'" >> ([0-9a-fA-F ]+?)(?: // (.*?))?"')
# "Current Watt: 0"
METRIC = re.compile(r'"Current ([A-Za-z][A-Za-z ]*): (-?[0-9.]+)"')
DEVICE = re.compile(r'"Found new device: "\s+"([^"]+)"')

# Only the metrics that come from a frame rather than from QZ's own bookkeeping. Distance,
# KCal and the averages are accumulated over the session, so they say nothing about the frame
# that happened to be parsed last.
FRAME_METRICS = {
    "Speed": "speed",
    "Cadence": "cadence",
    "Watt": "watts",
    "Resistance": "resistance",
    "Heart": "heart",
}


def epoch_ms(line):
    """QZ's second field is the epoch in milliseconds."""
    parts = line.split(None, 7)
    for part in parts:
        if part.isdigit() and len(part) >= 12:
            return int(part)
    return None


# ---------------------------------------------------------------------------------------
# Extract
# ---------------------------------------------------------------------------------------


def extract(path):
    events = []
    device = None
    base = None
    last_frame = None

    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            when = epoch_ms(line)

            if device is None:
                found = DEVICE.search(line)
                if found and not found.group(1).startswith("Bluetooth "):
                    device = found.group(1)

            match = NOTIFY.search(line)
            if match and when is not None:
                uuid, declared, hexbytes = match.groups()
                data = bytes.fromhex(hexbytes.replace(" ", ""))
                if base is None:
                    base = when
                last_frame = {
                    "kind": "<<",
                    "t": (when - base) / 1000.0,
                    "uuid": uuid.lower(),
                    "data": data,
                    "declared": int(declared),
                    "metrics": {},
                }
                events.append(last_frame)
                continue

            match = WRITE.search(line)
            if match and when is not None:
                hexbytes, info = match.groups()
                if base is None:
                    base = when
                events.append({
                    "kind": ">>",
                    "t": (when - base) / 1000.0,
                    "uuid": "?",
                    "data": bytes.fromhex(hexbytes.replace(" ", "")),
                    "info": (info or "").strip(),
                })
                last_frame = None
                continue

            # Metrics belong to the frame that produced them, and only until the next event.
            if last_frame is not None:
                for name, value in METRIC.findall(line):
                    key = FRAME_METRICS.get(name.strip())
                    if key and key not in last_frame["metrics"]:
                        last_frame["metrics"][key] = float(value)

    return {"source": path, "device": device, "events": events}


def write_fixture(recording, out):
    events = recording["events"]
    frames = [e for e in events if e["kind"] == "<<"]
    writes = [e for e in events if e["kind"] == ">>"]

    print("# qzlog2ride fixture - recorded FTMS traffic, replayable verbatim", file=out)
    print("# Times are seconds from the first recorded event. Bytes are as they went out.", file=out)
    print("# Trailing name=value pairs on a << line are the metrics QZ derived from it.", file=out)
    print("# A >> line has no UUID because the log does not record one.", file=out)
    print("", file=out)
    print("source   %s" % recording["source"], file=out)
    print("device   %s" % (recording["device"] or "unknown"), file=out)
    print("frames   %d" % len(frames), file=out)
    print("writes   %d" % len(writes), file=out)
    print("duration %.3f" % (events[-1]["t"] if events else 0.0), file=out)
    print("", file=out)

    for event in events:
        hexbytes = event["data"].hex()
        if event["kind"] == "<<":
            metrics = " ".join("%s=%s" % (k, fmt(v))
                               for k, v in sorted(event["metrics"].items()))
            line = "t=%-9.3f << %s %s" % (event["t"], event["uuid"], hexbytes)
            if metrics:
                line += "  " + metrics
        else:
            line = "t=%-9.3f >> %s  %s" % (event["t"], hexbytes, event["uuid"])
            if event["info"]:
                line += "  # " + event["info"]
        print(line, file=out)


def fmt(value):
    return ("%.3f" % value).rstrip("0").rstrip(".") if value % 1 else str(int(value))


# ---------------------------------------------------------------------------------------
# Read a fixture back
# ---------------------------------------------------------------------------------------


def read_fixture(path):
    header = {}
    events = []
    with open(path, "r", encoding="utf-8") as handle:
        for raw in handle:
            line = raw.split("#", 1)[0].strip() if not raw.lstrip().startswith("t=") else raw
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if line.startswith("t="):
                body, _, comment = line.partition("#")
                fields = body.split()
                t = float(fields[0][2:])
                kind = fields[1]
                if kind == "<<":
                    event = {"kind": "<<", "t": t, "uuid": fields[2],
                             "data": bytes.fromhex(fields[3]), "metrics": {}}
                    for pair in fields[4:]:
                        key, _, value = pair.partition("=")
                        event["metrics"][key] = float(value)
                else:
                    event = {"kind": ">>", "t": t, "data": bytes.fromhex(fields[2]),
                             "uuid": fields[3] if len(fields) > 3 else "?",
                             "info": comment.strip()}
                events.append(event)
            else:
                key, _, value = line.partition(" ")
                header[key] = value.strip()
    return {"header": header, "events": events}


# ---------------------------------------------------------------------------------------
# An independent Indoor Bike Data decoder, for --verify
# ---------------------------------------------------------------------------------------

# Field order and widths from the FTMS Indoor Bike Data characteristic. Bit 0 is "More Data":
# when it is set the instantaneous speed field is absent, which is how this bike splits its
# data across two notifications rather than sending one long one.
FIELDS = [
    (0, "speed", 2, 0.01, True),        # bit 0 set means ABSENT - handled below
    (1, "avg_speed", 2, 0.01, False),
    (2, "cadence", 2, 0.5, False),
    (3, "avg_cadence", 2, 0.5, False),
    (4, "distance", 3, 1, False),
    (5, "resistance", 2, 1, False),
    (6, "watts", 2, 1, False),
    (7, "avg_watts", 2, 1, False),
    (8, "energy_total", 2, 1, False),
    (8, "energy_per_hour", 2, 1, False),
    (8, "energy_per_minute", 1, 1, False),
    (9, "heart", 1, 1, False),
    (10, "metabolic", 1, 0.1, False),
    (11, "elapsed", 2, 1, False),
    (12, "remaining", 2, 1, False),
]


def decode_indoor_bike_data(data):
    if len(data) < 2:
        return None, "shorter than a flags word"
    flags = data[0] | (data[1] << 8)
    at = 2
    out = {}
    for bit, name, width, scale, inverted in FIELDS:
        present = (flags >> bit) & 1
        if inverted:
            present = not present
        if not present:
            continue
        if at + width > len(data):
            return out, "ran out of bytes at %s" % name
        raw = int.from_bytes(data[at:at + width], "little")
        at += width
        out[name] = raw * scale
    leftover = len(data) - at
    return out, ("%d byte(s) unaccounted for" % leftover) if leftover else None


# ---------------------------------------------------------------------------------------
# Commands
# ---------------------------------------------------------------------------------------


def verify(path):
    """Replay the recorded frames through an independent decoder and compare, per field.

    The point is to prove the *recording* is faithful: if a frame boundary were wrong, a
    frame dropped, or a UUID misattributed, the numbers would stop lining up with what QZ
    logged at the time.

    Reported per field rather than as one verdict, because not every metric QZ logs is a
    field it read. Cadence, resistance, power and heart rate are passed through from the
    frame. Speed on this bike is not: the trainer reports zero and QZ runs its own coast-down
    model over it, so a difference there is QZ working as designed and says nothing about the
    recording. A field that disagrees *and* is supposed to be pass-through is the failure.
    """
    PASS_THROUGH = ("cadence", "resistance", "watts", "heart")

    fixture = read_fixture(path)
    frames = [e for e in fixture["events"] if e["kind"] == "<<"]
    notes = {}
    per_field = {}
    skipped = 0

    for event in frames:
        if event["uuid"] != "2ad2":
            skipped += 1
            continue
        decoded, note = decode_indoor_bike_data(event["data"])
        if note:
            notes[note] = notes.get(note, 0) + 1
        if decoded is None:
            continue
        for key, expected in event["metrics"].items():
            stat = per_field.setdefault(key, {"compared": 0, "differed": 0, "absent": 0,
                                              "worst": 0.0, "example": None})
            if key not in decoded:
                stat["absent"] += 1
                continue
            stat["compared"] += 1
            delta = abs(decoded[key] - expected)
            if delta > 0.051:
                stat["differed"] += 1
                if delta > stat["worst"]:
                    stat["worst"] = delta
                    stat["example"] = (fmt(decoded[key]), fmt(expected))

    print("%s" % path)
    print("  %d frames, %d on 0x2AD2" % (len(frames), len(frames) - skipped))
    for note, count in sorted(notes.items(), key=lambda kv: -kv[1]):
        print("  the bike sends more than it flags: %s, x%d" % (note, count))

    failed = False
    for key in sorted(per_field):
        stat = per_field[key]
        verdict = "matches"
        if stat["absent"]:
            verdict = "QZ reported it %d time(s) from a frame that does not carry it" % stat["absent"]
            if key in PASS_THROUGH:
                failed = True
        elif stat["differed"]:
            example = " (frame %s, QZ %s)" % stat["example"] if stat["example"] else ""
            if key in PASS_THROUGH:
                verdict = "DIFFERS %d/%d%s" % (stat["differed"], stat["compared"], example)
                failed = True
            else:
                verdict = ("differs %d/%d%s - QZ derives this rather than reading it"
                           % (stat["differed"], stat["compared"], example))
        else:
            verdict = "matches %d/%d" % (stat["compared"], stat["compared"])
        print("  %-11s %s" % (key, verdict))

    if failed:
        print("  FAILED: a pass-through field does not survive the round trip")
        return 1
    print("  the recording is faithful: every pass-through field replays to what QZ logged")
    return 0


def oracle(path):
    """What QZ wrote, and nothing else - the half a change to QZ's output shows up in."""
    fixture = read_fixture(path)
    for event in fixture["events"]:
        if event["kind"] == ">>":
            line = "t=%-9.3f >> %s" % (event["t"], event["data"].hex())
            if event["info"]:
                line += "  # " + event["info"]
            print(line)
    return 0


def stats(path):
    fixture = read_fixture(path)
    events = fixture["events"]
    by_uuid = {}
    by_flags = {}
    for event in events:
        if event["kind"] != "<<":
            continue
        by_uuid[event["uuid"]] = by_uuid.get(event["uuid"], 0) + 1
        if event["uuid"] == "2ad2" and len(event["data"]) >= 2:
            flags = event["data"][0] | (event["data"][1] << 8)
            key = (flags, len(event["data"]))
            by_flags[key] = by_flags.get(key, 0) + 1
    writes = [e for e in events if e["kind"] == ">>"]

    for key, value in fixture["header"].items():
        print("%-9s %s" % (key, value))
    print("")
    print("notifications by characteristic:")
    for uuid, count in sorted(by_uuid.items(), key=lambda kv: -kv[1]):
        print("  0x%s  x%d" % (uuid, count))
    print("")
    print("0x2AD2 shapes (flags, length):")
    for (flags, length), count in sorted(by_flags.items(), key=lambda kv: -kv[1]):
        decoded, note = decode_indoor_bike_data(bytes([flags & 0xFF, flags >> 8]) + bytes(length - 2))
        fields = ", ".join(sorted(decoded or {}))
        print("  0x%04x len %-3d x%-6d %s" % (flags, length, count, fields))
        if note:
            print("       %s" % note)
    print("")
    print("writes: %d" % len(writes))
    kinds = {}
    for event in writes:
        kinds[event["info"] or "(no description)"] = kinds.get(event["info"] or "(no description)", 0) + 1
    for info, count in sorted(kinds.items(), key=lambda kv: -kv[1])[:12]:
        print("  %-40s x%d" % (info, count))
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("input", help="a QZ debug log, or a .frames fixture with --verify/--oracle/--stats")
    parser.add_argument("-o", "--output", help="where to write the fixture (default: stdout)")
    parser.add_argument("--verify", action="store_true",
                        help="decode a fixture's frames and check they reproduce QZ's metrics")
    parser.add_argument("--oracle", action="store_true", help="print only what QZ wrote")
    parser.add_argument("--stats", action="store_true", help="summarise a fixture")
    args = parser.parse_args()

    if args.verify:
        return verify(args.input)
    if args.oracle:
        return oracle(args.input)
    if args.stats:
        return stats(args.input)

    recording = extract(args.input)
    if not recording["events"]:
        print("no FTMS traffic in %s - was log_debug on?" % args.input, file=sys.stderr)
        return 2
    if args.output:
        with open(args.output, "w", encoding="utf-8", newline="\n") as out:
            write_fixture(recording, out)
        frames = sum(1 for e in recording["events"] if e["kind"] == "<<")
        writes = len(recording["events"]) - frames
        print("%s: %d frames, %d writes, %.1f s -> %s" %
              (recording["device"] or "unknown", frames, writes,
               recording["events"][-1]["t"], args.output))
    else:
        write_fixture(recording, sys.stdout)
    return 0


if __name__ == "__main__":
    sys.exit(main())
