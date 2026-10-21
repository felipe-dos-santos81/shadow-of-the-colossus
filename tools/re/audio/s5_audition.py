#!/usr/bin/env python3
"""S5 audition — decode the curated cue map to WAV and gate it.

Reads `tools/re/audio/s5_map.json`, decodes each mapped `(bank, wave)` with the
loader-derived payload formula (`wave_record_offset + 0x24 + start`, see
`docs/notes/s5-audio-recon.md` §7), writes WAVs under
`out/s5/audition/<bank>/`, and with `--check` fails if:

* any of the seven core events has no cue row with a valid wave, or is
  `status=deferred`, or is also listed as deferred;
* a `ghidra` row is missing `reqd`/`tone`, its `reqd` does not match the NAME
  hash, the actual `SEQD`/`PLAY_TONE` walk does not return the mapped tone, or
  `TONE[tone].wave` does not equal the mapped wave;
* an `audition` row does not set `verified_index` (identity is not claimed);
* two cue rows share a name, or a row's `layers` is malformed;
* any layer in a cue is missing, silent, not 48 kHz, or shorter/longer than the
  record by more than 50 ms.  `layers` is the full set including the primary
  (`layers[0] == wave`); a multi-layer cue also gets an equal-gain mix WAV.

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
import wave

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


def reqd_sequence(buf, chunks, reqd):
    """REQD[reqd] +0x0C: the SEQD byte offset the request plays."""
    if "REQD" not in chunks or "SEQD" not in chunks:
        return None
    table = gate.table_records(buf, chunks["REQD"][0], "<audition>")
    if not 0 <= reqd < len(table):
        return None
    return struct.unpack_from("<I", buf, table[reqd][0] + 0x0C)[0]


def simple_operand(buf, pos):
    """Decode a 0x00/0x10 (decimal) PLAY_TONE operand; None for complex types."""
    if pos >= len(buf):
        return None
    b0 = buf[pos]
    typ = b0 >> 4
    n = b0 & 0x0F
    if typ not in (0, 1) or n == 0 or pos + n >= len(buf):
        return None
    value = 0
    for k in range(n):
        x = buf[pos + 1 + k]
        value = (x & 0x0F) + value * 100 + (x >> 4) * 10
    return -value if typ == 1 else value


def walk_play_tone(bank, reqd, scan=4096):
    """Walk REQD -> SEQD timeline/sub-table -> first simple `a4` tone.

    Timeline entry (20 B): u16 +0x04 == 0x4000, next offset u32 +0x0C.
    Sub-table entry (24 B): u16 +0x0C == 0x4000, next offset u32 +0x14.
    Offsets are 4-byte aligned before following.  Returns None when the
    bytecode only has complex/random PLAY_TONE operands (e.g. the voice
    envelopes), which is a hard failure for a `ghidra` row.
    """
    buf = bank["buf"]
    seq = reqd_sequence(buf, bank["chunks"], reqd)
    if seq is None:
        return None
    so, ss = bank["chunks"]["SEQD"]
    for cand in (seq, seq & ~3, (seq + 3) & ~3):
        p = so + cand
        seen = set()
        followed = False
        for _ in range(8):
            if p + 24 > len(buf):
                break
            if struct.unpack_from("<H", buf, p + 0x04)[0] == 0x4000:
                nxt = struct.unpack_from("<I", buf, p + 0x0C)[0] & ~3
                followed = True
            elif struct.unpack_from("<H", buf, p + 0x0C)[0] == 0x4000:
                nxt = struct.unpack_from("<I", buf, p + 0x14)[0] & ~3
                followed = True
            else:
                break
            if nxt in seen:
                break
            seen.add(nxt)
            p = so + nxt
        if not followed:
            continue
        for j in range(0, min(scan, len(buf) - p - 2)):
            if buf[p + j] == 0xA4:
                tone = simple_operand(buf, p + j + 1)
                if tone is not None and 0 <= tone < 1000:
                    return tone
    return None


def read_wav_pcm(path):
    """Return (channels, rate, [channel sample lists]) from a pcm_s16le WAV."""
    with wave.open(path, "rb") as fh:
        channels = fh.getnchannels()
        rate = fh.getframerate()
        frames = fh.getnframes()
        raw = fh.readframes(frames)
    data = struct.unpack("<%dh" % (frames * channels), raw) if raw else ()
    out = [[] for _ in range(channels)]
    for i, value in enumerate(data):
        out[i % channels].append(value)
    return channels, rate, out


def mix_layers(paths, out_path):
    """Equal-gain mix of decoded layer WAVs; returns (channels, rate, frames)."""
    layers = [read_wav_pcm(p) for p in paths]
    channels, rate = layers[0][0], layers[0][1]
    for c, r, _ in layers:
        if c != channels or r != rate:
            raise AuditionError(
                f"layer format mismatch: {c}ch/{r}Hz vs {channels}ch/{rate}Hz")
    frames = max(len(pcm[0]) for _, _, pcm in layers)
    mix = [[0.0] * frames for _ in range(channels)]
    # sum per channel, then divide by the number of layers (equal gain)
    for _, _, pcm in layers:
        for ch in range(channels):
            src = pcm[ch]
            dst = mix[ch]
            for i, value in enumerate(src):
                dst[i] += value
    gain = 1.0 / len(layers)
    with wave.open(out_path, "wb") as fh:
        fh.setnchannels(channels)
        fh.setsampwidth(2)
        fh.setframerate(rate)
        out = bytearray()
        for i in range(frames):
            for ch in range(channels):
                value = int(round(mix[ch][i] * gain))
                value = -32768 if value < -32768 else (32767 if value > 32767 else value)
                out += struct.pack("<h", value)
        fh.writeframes(bytes(out))
    return channels, rate, frames


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

    # --- map structure: every core event needs one decoded cue row ---
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
            continue
        if cue.get("status") == "deferred":
            problems.append(f"{event}: core cue is status=deferred")
        if not isinstance(cue.get("wave"), int) or cue["wave"] < 0:
            problems.append(f"{event}: core cue has no valid wave")
        if event in deferred_events:
            problems.append(f"{event}: core cue is also listed as deferred")

    rates_used = set()
    # --- identity + decode, including every layer ---
    for cue in cues:
        event = cue.get("event", "?")
        if cue.get("status") == "deferred" or cue.get("wave") is None:
            print(f"  {event:<16} deferred: {cue.get('reason', 'no wave')}")
            if event in CORE_EVENTS:
                problems.append(f"{event}: core cue is deferred")
            continue
        bank = load_bank(args.root, cue["bank"])
        reqd = lookup_name_id(bank["buf"], bank["chunks"], cue.get("name", ""))
        if reqd is None:
            problems.append(f"{event}: name {cue.get('name')!r} is not in the NAME table")
        elif cue.get("reqd") is None:
            problems.append(f"{event}: row is missing reqd")
        elif int(cue["reqd"]) != reqd:
            problems.append(f"{event}: reqd {cue['reqd']} != NAME lookup {reqd}")
        method = cue.get("method")
        wave = int(cue["wave"])
        layers = cue.get("layers") or [wave]
        if not isinstance(layers, list) or wave not in layers:
            problems.append(f"{event}: layers must be a list containing the primary wave")
            layers = [wave]
        if layers and layers[0] != wave:
            problems.append(f"{event}: primary wave must be layers[0]")
        if len(set(layers)) != len(layers):
            problems.append(f"{event}: layers contains duplicates")
        if method == "ghidra":
            tone = cue.get("tone")
            if tone is None:
                problems.append(f"{event}: ghidra row is missing tone")
            elif cue.get("reqd") is None:
                problems.append(f"{event}: ghidra row is missing reqd")
            else:
                walked = walk_play_tone(bank, int(cue["reqd"]))
                if walked != int(tone):
                    problems.append(
                        f"{event}: walked PLAY_TONE {walked} != mapped tone {tone}")
                tw = tone_wave(bank["buf"], bank["chunks"], int(tone))
                if tw != wave:
                    problems.append(
                        f"{event}: TONE[{tone}].wave={tw} != mapped wave {wave}")
                if walked == int(tone) and tw == wave:
                    print(f"  {event:<16} chain: reqd {reqd} -> PLAY_TONE {tone} "
                          f"-> wave {tw}")
        elif method == "audition":
            if not cue.get("verified_index"):
                problems.append(f"{event}: audition row must set verified_index")
            else:
                print(f"  {event:<16} audition: wave {wave} "
                      "(identity not chain-proven)")
        else:
            problems.append(f"{event}: unknown method {method!r}")

        out_dir = os.path.join(args.out, cue["bank"].split(".")[0])
        layer_paths = []
        primary_stats = None
        for layer in layers:
            label = cue.get("name") if len(layers) == 1 else f"{cue.get('name')}_layer{layer}"
            try:
                wav_path, stats = decode_wave(bank, int(layer), out_dir, label=label)
            except (AuditionError, gate.GateError) as exc:
                problems.append(f"{event}: layer {layer}: {exc}")
                continue
            layer_paths.append(wav_path)
            rates_used.add(stats["rate"])
            layer_problems = check_stats(stats)
            if layer_problems:
                problems.extend(f"{event} layer {layer}: {p}" for p in layer_problems)
                print(f"  {event:<16} FAIL layer {layer}: {'; '.join(layer_problems)}")
            else:
                print(f"  {event:<16} {cue['bank']:<12} w{int(layer):03d} "
                      f"{cue.get('name', ''):<28} "
                      f"{stats['duration_s']:.3f} s  {stats['rate']} Hz  "
                      f"RMS {stats['rms_dbfs']:.1f} dBFS")
            if int(layer) == wave:
                primary_stats = stats
        if len(layers) > 1 and len(layer_paths) == len(layers):
            mix_path = os.path.join(out_dir, safe(event) + "_mix.wav")
            try:
                ch, rate, frames = mix_layers(layer_paths, mix_path)
                pcm = read_wav_pcm(mix_path)[2]
                rms = 0.0
                for c in pcm:
                    rms += sum(v * v for v in c)
                total = frames * ch
                rms = math.sqrt(rms / total) / 32768.0 if total else 0.0
                mix_problems = []
                if rate != 48000:
                    mix_problems.append(f"rate {rate} Hz != 48000")
                if frames == 0:
                    mix_problems.append("empty mix")
                mix_db = 20.0 * math.log10(rms) if rms > 0 else -999.0
                if mix_db <= RMS_FLOOR_DBFS:
                    mix_problems.append("silent mix")
                rates_used.add(rate)
                if mix_problems:
                    problems.extend(f"{event} mix: {p}" for p in mix_problems)
                    print(f"  {event:<16} FAIL mix: {'; '.join(mix_problems)}")
                else:
                    print(f"  {event:<16} mixed {len(layers)} layers -> "
                          f"{os.path.relpath(mix_path, args.out)}")
            except (AuditionError, gate.GateError) as exc:
                problems.append(f"{event}: mix failed: {exc}")
        decoded += 1
    if args.check and problems:
        print(f"AUDITION FAILED: {len(problems)} problem(s)", file=sys.stderr)
        for p in problems:
            print(f"  {p}", file=sys.stderr)
        return 1
    if args.check:
        rate_text = ", ".join(f"{r}" for r in sorted(rates_used)) or "none"
        print(f"AUDITION PASSED: {decoded} cue(s), {len(rates_used)} rate(s) "
              f"({rate_text} Hz), all layers non-silent")
    else:
        print(f"decoded {decoded} mapped cue(s) (run with --check to gate)")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (AuditionError, gate.GateError) as exc:
        print(f"AUDITION ERROR: {exc}", file=sys.stderr)
        sys.exit(2)
