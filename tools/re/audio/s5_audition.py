#!/usr/bin/env python3
"""S5 audition — decode the curated cue map to WAV and gate it.

Reads `tools/re/audio/s5_map.json`, decodes each mapped `(bank, wave)` with the
loader-derived payload formula (`wave_record_offset + 0x24 + start`, see
`docs/notes/s5-audio-recon.md` §7), writes WAVs under
`out/s5/audition/<bank>/`, and with `--check` fails if:

* any of the seven core events has no cue row with a valid wave (or is also
  listed as deferred);
* a `ghidra` row's `reqd` does not match the NAME hash, or its
  `TONE[tone].wave` does not equal the mapped wave;
* an `audition` row does not set `verified_index` (identity is not claimed);
* two cue rows share a name;
* a mapped wave is missing, silent, not 48 kHz, or shorter/longer than the
  record by more than 50 ms.

Usage
  python3 tools/re/audio/s5_audition.py --root "$SOTC_GAME_ROOT" \
      --map tools/re/audio/s5_map.json --out out/s5/audition --check
  python3 tools/re/audio/s5_audition.py --root "$SOTC_GAME_ROOT" \
      --bank boss01 --all --out out/s5/audition     # decode every wave

Read-only on the game tree; every artifact lands under `--out`.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import s5_decode_gate as gate  # noqa: E402

RMS_FLOOR_DBFS = -60.0
CORE_EVENTS = ("valus.telegraph", "valus.impact", "valus.hurt",
               "player.swing", "player.hit", "player.hurt", "player.death")


class AuditionError(RuntimeError):
    pass


def bank_path(root, bank):
    if not bank.endswith(".sxd") and not bank.endswith(".sxd1"):
        bank = bank + ".sxd"
    return os.path.join(root, "sound", "shadow", bank)


def load_bank(root, bank):
    """Return a dict with buf, chunks and fully-addressed WAVE records."""
    path = bank_path(root, bank)
    buf = gate.read_bytes(path)
    chunks = gate.chunk_map(buf, path)
    if "WAVE" not in chunks or "DATA" not in chunks:
        raise AuditionError(f"{path}: no WAVE/DATA chunk")
    wave_off, wave_size = chunks["WAVE"]
    data_off, data_size = chunks["DATA"]
    payload_base = data_off + 0x10
    payload_end = data_off + 8 + data_size
    table = gate.table_records(buf, wave_off, path)
    records = [gate.read_wave_record(buf, rec, path) for rec, _tag in table]
    for k, r in enumerate(records):
        nxt = table[k + 1][0] if k + 1 < len(table) else wave_off + 8 + wave_size
        r["stride"] = nxt - r["offset"]
        r["at9_hint"] = None
        for off in range(0x28, max(0x28, r["stride"] - 3), 4):
            value = struct.unpack_from("<I", buf, r["offset"] + off)[0]
            if value >> 24 == 0xFE:
                r["at9_hint"] = value
                break
        r["payload"] = r["offset"] + 0x24 + r["start"]
        if r["payload"] < payload_base or r["payload"] + r["bytes"] > payload_end:
            raise AuditionError(
                f"{path}: record {k} payload escapes DATA")
    names = bank_names(buf, chunks)
    return {"path": path, "buf": buf, "chunks": chunks, "records": records,
            "names": names, "payload_base": payload_base}


def name_hash(name):
    """The engine's name hash (`FUN_00987a80`): u = c + u*0x83."""
    u = 0
    for c in name.encode("latin1"):
        u = (c + u * 0x83) & 0xFFFFFFFF
    return u


def lookup_name_id(buf, chunks, name):
    """NAME hash -> REQD id, or None (the exact `FUN_00987a80` walk)."""
    if "NAME" not in chunks:
        return None
    no, ns = chunks["NAME"]
    count = struct.unpack_from("<I", buf, no + 0x0C)[0]
    base = no + 0x10
    hv = name_hash(name)
    for i in range(count):
        e = base + 12 * i
        so, h, idx = struct.unpack_from("<III", buf, e)
        if h != hv:
            continue
        s = buf[e + so:buf.find(b"\0", e + so)].decode("latin1")
        if s == name:
            return idx
    return None


def tone_wave(buf, chunks, tone):
    """TONE[tone].wave from the flat 12-byte table at TONE+0x20 (boss01 form)."""
    if "TONE" not in chunks or tone is None:
        return None
    to, ts = chunks["TONE"]
    off = to + 0x20 + 12 * tone
    if tone < 0 or off + 12 > to + 8 + ts:
        return None
    return struct.unpack_from("<6H", buf, off)[4]


def bank_names(buf, chunks):
    """The name pool in insertion order (WAVE order follows it for the first N)."""
    if "NAME" not in chunks:
        return []
    no, ns = chunks["NAME"]
    count = struct.unpack_from("<I", buf, no + 0x0C)[0]
    pool = no + 0x10 + 12 * count
    return [x.decode("latin1") for x in buf[pool:no + 8 + ns].split(b"\0") if x]


def wave_name(bank, index):
    names = bank["names"]
    if 0 <= index < len(names):
        return names[index]
    return f"w{index:03d}"


def safe(label):
    return "".join(c if c.isalnum() or c in "-_." else "_" for c in label)


def decode_wave(bank, index, out_dir, label=None):
    """Decode one WAVE record; return (wav_path, stats)."""
    records = bank["records"]
    if not 0 <= index < len(records):
        raise AuditionError(f"{bank['path']}: wave index {index} out of range "
                            f"(0..{len(records) - 1})")
    rec = records[index]
    buf = bank["buf"]
    base = rec["payload"]
    blob = buf[base:base + rec["bytes"]]
    if len(blob) != rec["bytes"]:
        raise AuditionError(f"{bank['path']}: short read for wave {index}")
    os.makedirs(out_dir, exist_ok=True)
    codec = rec["fmt"] & 0xFF
    label = safe(label if label is not None else wave_name(bank, index))
    name = f"w{index:03d}_{label}"
    wav_path = os.path.join(out_dir, name + ".wav")
    if codec == 0x21:
        pcm = gate.ps_adpcm_decode(blob, rec["channels"])
        gate.write_wav_s16(wav_path, rec["channels"], rec["rate"], pcm)
        frames = min(len(c) for c in pcm)
        total = frames * rec["channels"]
        acc = 0
        peak = 0
        for c in pcm:
            for v in c:
                acc += v * v
                peak = max(peak, abs(v))
        rms = math.sqrt(acc / total) / 32768.0 if total else 0.0
        stats = {"codec": "ps_adpcm", "frames": frames,
                 "duration_s": frames / rec["rate"],
                 "expected_frames": rec["samples"],
                 "expected_duration_s": rec["samples"] / rec["rate"],
                 "duration_error_ms": abs(frames - rec["samples"]) / rec["rate"] * 1e3,
                 "rms_dbfs": 20.0 * math.log10(rms) if rms > 0 else -999.0,
                 "peak": peak / 32768.0, "finite": True, "rate": rec["rate"]}
        return wav_path, stats
    if codec == 0x42:
        if not rec["at9_hint"]:
            raise AuditionError(f"{bank['path']}: wave {index} has no AT9 config")
        hint = rec["at9_hint"]
        frame_bytes = ((hint >> 5) & 0x7FF) + 1
        superframe = (hint >> 3) & 3
        riff = gate.at9_riff_with_config(blob, rec["channels"], rec["rate"],
                                         frame_bytes << superframe,
                                         hint.to_bytes(4, "big"))
        at9_path = os.path.join(out_dir, name + ".at9.wav")
        with open(at9_path, "wb") as fh:
            fh.write(riff)
        raw_path = os.path.join(out_dir, name + ".f32")
        cmd = ["ffmpeg", "-hide_banner", "-v", "error", "-y", "-i", at9_path,
               "-map", "0:a", "-c:a", "pcm_f32le", "-f", "f32le", raw_path,
               "-c:a", "pcm_s16le", wav_path]
        proc = subprocess.run(cmd, capture_output=True, text=True)
        if proc.returncode != 0:
            raise AuditionError(f"ffmpeg failed for {name}: {proc.stderr.strip()}")
        stats = gate.analyze_f32(raw_path, wav_path, rec["channels"],
                                 rec["rate"], rec["samples"])
        stats["codec"] = "atrac9"
        stats["rate"] = rec["rate"]
        return wav_path, stats
    raise AuditionError(f"{bank['path']}: wave {index} format 0x{codec:02X} "
                        "has no decoder in this audition")


def check_stats(stats):
    problems = []
    if stats.get("rate") != 48000:
        problems.append(f"rate {stats.get('rate')} Hz != 48000")
    if not stats.get("finite", True):
        problems.append("non-finite samples")
    if stats["duration_error_ms"] > 50.0:
        problems.append(f"duration error {stats['duration_error_ms']:.1f} ms")
    if stats["rms_dbfs"] <= RMS_FLOOR_DBFS:
        problems.append(f"RMS {stats['rms_dbfs']:.1f} dBFS <= {RMS_FLOOR_DBFS:.0f}")
    return problems


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", required=True, help="game root (contains sound/shadow)")
    ap.add_argument("--map", default="tools/re/audio/s5_map.json",
                    help="cue map to decode")
    ap.add_argument("--out", default="out/s5/audition", help="artifact directory")
    ap.add_argument("--check", action="store_true",
                    help="fail on a missing/silent/short mapped wave")
    ap.add_argument("--bank", help="decode a whole bank with --all")
    ap.add_argument("--all", action="store_true",
                    help="decode every wave of --bank (exploration)")
    ap.add_argument("--list", action="store_true", help="list bank waves and exit")
    args = ap.parse_args(argv)

    if args.all:
        if not args.bank:
            print("AUDITION ERROR: --all needs --bank", file=sys.stderr)
            return 2
        bank = load_bank(args.root, args.bank)
        out_dir = os.path.join(args.out, args.bank.split(".")[0])
        for i in range(len(bank["records"])):
            wav, stats = decode_wave(bank, i, out_dir)
            print(f"  w{i:03d} {wave_name(bank, i):<28} "
                  f"{stats['duration_s']:.3f} s  RMS {stats['rms_dbfs']:.1f} dBFS")
        print(f"decoded {len(bank['records'])} waves -> {out_dir}")
        return 0
    if args.list:
        if not args.bank:
            print("AUDITION ERROR: --list needs --bank", file=sys.stderr)
            return 2
        bank = load_bank(args.root, args.bank)
        for i, rec in enumerate(bank["records"]):
            print(f"w{i:03d}\t{wave_name(bank, i)}\t{rec['fmt']:#010x}\t"
                  f"{rec['samples'] / rec['rate']:.3f}s")
        return 0

    with open(args.map) as fh:
        cue_map = json.load(fh)
    cues = cue_map.get("cues", [])
    deferred = cue_map.get("deferred", [])
    problems = []
    decoded = 0

    # --- map structure: every core event needs one cue row with a valid wave ---
    seen_events = {}
    seen_names = {}
    for cue in cues:
        event = cue.get("event")
        if event in seen_events:
            problems.append(f"duplicate cue event {event!r}")
        seen_events[event] = cue
        name = cue.get("name")
        if name:
            if name in seen_names:
                problems.append(f"duplicate cue name {name!r}")
            seen_names[name] = event
    deferred_events = {d.get("event") for d in deferred}
    for event in CORE_EVENTS:
        cue = seen_events.get(event)
        if cue is None:
            problems.append(f"{event}: core cue has no row in cues")
        elif not isinstance(cue.get("wave"), int) or cue["wave"] < 0:
            problems.append(f"{event}: core cue has no valid wave")
        if event in deferred_events:
            problems.append(f"{event}: core cue is also listed as deferred")

    # --- identity: chain-verified rows must walk, audition rows must be explicit ---
    for cue in cues:
        event = cue.get("event", "?")
        if cue.get("status") == "deferred" or cue.get("wave") is None:
            print(f"  {event:<16} deferred: {cue.get('reason', 'no wave')}")
            continue
        bank = load_bank(args.root, cue["bank"])
        reqd = lookup_name_id(bank["buf"], bank["chunks"], cue.get("name", ""))
        if reqd is None:
            problems.append(f"{event}: name {cue.get('name')!r} is not in the NAME table")
        elif cue.get("reqd") is not None and int(cue["reqd"]) != reqd:
            problems.append(f"{event}: reqd {cue['reqd']} != NAME lookup {reqd}")
        method = cue.get("method")
        if method == "ghidra":
            tone = cue.get("tone")
            if tone is None:
                problems.append(f"{event}: ghidra row is missing tone")
            else:
                tw = tone_wave(bank["buf"], bank["chunks"], int(tone))
                if tw != int(cue["wave"]):
                    problems.append(
                        f"{event}: TONE[{tone}].wave={tw} != mapped wave {cue['wave']}")
                else:
                    print(f"  {event:<16} chain: reqd {reqd} -> tone {tone} -> wave {tw}")
        elif method == "audition":
            if not cue.get("verified_index"):
                problems.append(f"{event}: audition row must set verified_index")
            else:
                print(f"  {event:<16} audition: wave {cue['wave']} "
                      "(identity not chain-proven)")
        else:
            problems.append(f"{event}: unknown method {method!r}")

        out_dir = os.path.join(args.out, cue["bank"].split(".")[0])
        wav, stats = decode_wave(bank, int(cue["wave"]), out_dir,
                                 label=cue.get("name"))
        cue_problems = check_stats(stats)
        decoded += 1
        if cue_problems:
            problems.extend(f"{event}: {p}" for p in cue_problems)
            print(f"  {event:<16} FAIL {'; '.join(cue_problems)}")
        else:
            print(f"  {event:<16} {cue['bank']:<12} w{int(cue['wave']):03d} "
                  f"{cue.get('name', ''):<28} "
                  f"{stats['duration_s']:.3f} s  {stats['rate']} Hz  "
                  f"RMS {stats['rms_dbfs']:.1f} dBFS")
    if args.check and problems:
        print(f"AUDITION FAILED: {len(problems)} problem(s)", file=sys.stderr)
        for p in problems:
            print(f"  {p}", file=sys.stderr)
        return 1
    if args.check:
        print(f"AUDITION PASSED: {decoded} mapped wave(s), all 48000 Hz, non-silent")
    else:
        print(f"decoded {decoded} mapped wave(s) (run with --check to gate)")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (AuditionError, gate.GateError) as exc:
        print(f"AUDITION ERROR: {exc}", file=sys.stderr)
        sys.exit(2)
