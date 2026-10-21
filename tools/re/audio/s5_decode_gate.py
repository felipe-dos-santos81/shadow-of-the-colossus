#!/usr/bin/env python3
"""S5 decode gate — prove SXDF carving + ATRAC9 decode before any S5 work.

Read-only on the game tree; every artifact lands under `--out` (gitignored).

What is measured here (2026-10-21, docs/notes/s5-audio-recon.md):

Container (`SXDF` banks and `SXDS` stream payloads)
  0x00  char[4]  "SXDF" (index+bank) | "SXDS" (stream payload)
  0x04  u16      version = 4
  0x06  u16      per-file, unverified
  0x08  u32      SXDF: own file size | SXDS: stream count
  0x0C  u32      SXDF: companion .sxd2 size, else 0 | SXDS: payload size
  0x10  u8[16]   bank GUID, identical in the .sxd1/.sxd2 pair
  0x20  char[32] bank name, NUL padded
  0x60  chunk chain: char[4] magic + u32 size (bytes AFTER the 8-byte header)

Generic table chunk (REQD, NAME, WAVE, SEQD)
  +0x04 u32 size, +0x08 u32 0, +0x0C u32 count, +0x10 u32 offsets[count]
  record_i = chunk + 0x10 + offsets[i] + 4*i     <- offsets[] are short by 4 B
                                                    per entry; the +4*i fixes it

`music.sxd1` WAVE record == 76 B = 19 u32
  +0x00 tag (3)          +0x04 format word   +0x08 sample rate (0xBB80 = 48000)
  +0x0C codec word        +0x10 0            +0x14 samples (per channel)
  +0x18/0x1C -1           +0x20 byte length  +0x24 start offset
  The start offsets are ABSOLUTE byte offsets in `music.sxd2` (record 0 = 0x60,
  the byte after the SXDS header); sorted (start, bytes) partitions the payload.

`<bank>.sxd` resident WAVE record: same field slots, variable stride (48/68 B on
boss01: the offsets[] rule already encodes the stride, so records are addressed
by `record_i` and only the fixed prefix is read).

Codec
  Music is ATRAC9, and the format word's second byte is the channel count:
  0x00FF0642 = 6 channels (5.1).  The AT9 config word is 4 bytes big-endian
  `FE <sri:4><blockcfg:3><verify:1> <frame_bytes-1:11><superframe:2>`, confirmed
  against the engine's own unpacker at eboot.elf vaddr 0x00984240 (bits 5..15 =
  frame bytes - 1 via `bextr 0xB05`, bits 17..19 = block config via `bextr
  0x311`, top byte compared with 0xFE000000).  ffmpeg's `atrac9` decoder wants
  that word as extradata: LE32 version 0 + the 4 config bytes + 4 pad bytes,
  inside a WAVEFORMATEXTENSIBLE `fmt ` chunk whose GUID is the AT9 one.

  Resident payload addressing (loader-derived, 2026-10-21): a WAVE record's
  data is at `record_offset + 0x24 + start` and is exactly `bytes` long.  The
  `start` field is relative to the record, not to the DATA heap; this is why
  the raw `start` values overlap.  The formula tiles the DATA payload exactly
  on boss01, com_obj and com_v.

  Resident codecs: the 0x21 family is 16-byte / 28-sample PS-ADPCM (the
  format switch in eboot.elf sets 0x594 = channels<<4 and 0x590 = 0x1c), with
  an 8-entry predictor table (filters 0-4 are the standard VAG table; 5-7 are
  fitted from block-boundary continuity because the real table lives in the
  NGS2 firmware).  The 0x42 family is ATRAC9 at 96 kbps/channel; the record's
  config word is the first 0xFE-prefixed dword at/after +0x28.

Usage
  python3 tools/re/audio/s5_decode_gate.py --root "$SOTC_GAME_ROOT" \
      --out out/s5/gate --check
"""

from __future__ import annotations

import argparse
import json
import math
import os
import shutil
import struct
import subprocess
import sys
import wave

CHUNK_BASE = 0x60
HEADER_SIZE = 0x60

# KSDATAFORMAT_SUBTYPE_ATRAC9 as it appears in the "fmt " chunk (libavformat
# riff.c: ff_codec_wav_guids).
AT9_GUID = bytes.fromhex("d242e147ba368d4d88fc61654f8c836c")

# AT9 sample-rate index (libatrac9 Tables.SampleRates); only 48 kHz is shipped.
SAMPLE_RATE_INDEX = {48000: 7}

# channels -> AT9 substream layout index (at9_block_layout)
BLOCK_CONFIG = {1: 0, 2: 2, 4: 5, 6: 3, 8: 4}

# PS-ADPCM predictor coefficients.  Filters 0-4 are the standard VAG table;
# filters 5-7 were fitted from block-boundary continuity (the engine's table
# lives in the NGS2 firmware, not in eboot.elf).  Rounded to integers.
VAG_COEF = ((0, 0), (60, 0), (115, -52), (98, -55), (122, -60),
            (126, -62), (128, -64), (126, -63))

RMS_FLOOR_DBFS = -60.0
DURATION_TOLERANCE_S = 0.050


class GateError(RuntimeError):
    pass


# --------------------------------------------------------------------------- #
# container
# --------------------------------------------------------------------------- #


def read_bytes(path):
    with open(path, "rb") as fh:
        return fh.read()


def parse_header(buf, path):
    if len(buf) < HEADER_SIZE:
        raise GateError(f"{path}: shorter than the 0x60 header")
    magic = buf[0:4].decode("latin1")
    if magic not in ("SXDF", "SXDS"):
        raise GateError(f"{path}: magic {magic!r} is not SXDF/SXDS")
    version, unknown06 = struct.unpack_from("<HH", buf, 0x04)
    size_or_streams = struct.unpack_from("<I", buf, 0x08)[0]
    companion = struct.unpack_from("<I", buf, 0x0C)[0]
    name = buf[0x20:0x40].split(b"\0")[0].decode("latin1")
    return {
        "path": path,
        "magic": magic,
        "version": version,
        "unknown06": unknown06,
        "size_or_streams": size_or_streams,
        "companion": companion,
        "guid": buf[0x10:0x20].hex(),
        "name": name,
        "size": len(buf),
    }


def walk_chunks(buf, path):
    """Yield (magic, offset, size) walking 0x60 to EOF.

    `size` counts every byte after the 8-byte chunk header, so the next chunk
    sits at offset + size + 8.
    """
    out = []
    off = CHUNK_BASE
    while off + 8 <= len(buf):
        magic = buf[off:off + 4]
        if not all(0x30 <= b <= 0x5A for b in magic):  # [0-9A-Z]
            break
        size = struct.unpack_from("<I", buf, off + 4)[0]
        out.append((magic.decode("latin1"), off, size))
        if size == 0:
            break
        off += size + 8
    if not out:
        raise GateError(f"{path}: no chunks at 0x60")
    return out


def chunk_map(buf, path):
    return {magic: (off, size) for magic, off, size in walk_chunks(buf, path)}


def table_records(buf, off, path):
    """Return [(record_offset, tag)] for a table chunk (see the module doc)."""
    size = struct.unpack_from("<I", buf, off + 4)[0]
    count = struct.unpack_from("<I", buf, off + 0x0C)[0]
    body = off + 0x10
    if count == 0:
        return []
    if body + 4 * count > off + 8 + size:
        raise GateError(f"{path}: {buf[off:off + 4]!r} count {count} overruns")
    offsets = struct.unpack_from("<%dI" % count, buf, body)
    records = []
    for i, rel in enumerate(offsets):
        rec = body + rel + 4 * i
        if rec + 4 > len(buf):
            raise GateError(f"{path}: record {i} beyond EOF")
        records.append((rec, struct.unpack_from("<I", buf, rec)[0]))
    return records


# --------------------------------------------------------------------------- #
# WAVE records
# --------------------------------------------------------------------------- #


def read_wave_record(buf, rec, path):
    """Read the fixed 0x28-byte prefix shared by the 76 B and 44/48 B records."""
    if rec + 0x28 > len(buf):
        raise GateError(f"{path}: WAVE record at {rec:#x} truncated")
    tag, fmt, rate, codec_word, zero, samples, loop_start, loop_end, nbytes, start = \
        struct.unpack_from("<10I", buf, rec)
    channels = (fmt >> 8) & 0xFF
    return {
        "offset": rec,
        "tag": tag,
        "fmt": fmt,
        "channels": channels,
        "rate": rate,
        "codec_word": codec_word,
        "samples": samples,
        # -1 = no loop; otherwise a (start, end) sample pair.
        "loop_start": loop_start,
        "loop_end": loop_end,
        "bytes": nbytes,
        "start": start,
    }


def verify_music_partition(records, payload_size, path):
    """The 33 (start, bytes) pairs must tile the .sxd2 payload exactly."""
    ordered = sorted(records, key=lambda r: r["start"])
    if ordered[0]["start"] != CHUNK_BASE:
        raise GateError(
            f"{path}: first start {ordered[0]['start']:#x} != payload base {CHUNK_BASE:#x}")
    gap = 0
    for a, b in zip(ordered, ordered[1:]):
        step = b["start"] - (a["start"] + a["bytes"])
        if step != 0:
            raise GateError(
                f"{path}: gap {step} B before start {b['start']:#x} (not a partition)")
        gap += abs(step)
    end = ordered[-1]["start"] + ordered[-1]["bytes"]
    if end != CHUNK_BASE + payload_size:
        raise GateError(f"{path}: partition ends {end:#x}, payload ends {CHUNK_BASE + payload_size:#x}")
    return {"records": len(ordered), "payload_bytes": payload_size, "gaps": gap,
            "first_start": ordered[0]["start"], "end": end}


# --------------------------------------------------------------------------- #
# ATRAC9
# --------------------------------------------------------------------------- #


def at9_config(sri, block_config, frame_bytes, superframe_index):
    """Pack the 4-byte AT9 config: FE | sri:4 cfg:3 verify:1 | (frames-1):11 | sf:2."""
    if not 0 <= sri <= 15:
        raise GateError(f"AT9 sample-rate index {sri} out of range")
    if not 0 <= block_config <= 5:
        raise GateError(f"AT9 block config {block_config} out of range")
    if not 1 <= frame_bytes <= 2048:
        raise GateError(f"AT9 frame bytes {frame_bytes} out of range")
    if superframe_index not in (0, 2):
        raise GateError(f"AT9 superframe index {superframe_index} must be 0 or 2")
    a = frame_bytes - 1
    return bytes([
        0xFE,
        (sri << 4) | (block_config << 1),
        (a >> 3) & 0xFF,
        ((a & 7) << 5) | ((superframe_index & 3) << 3),
    ])


def at9_geometry(record):
    """Derive (superframe_bytes, frame_bytes) from the record's byte/sample ratio.

    One ATRAC9 superframe covers 1024 samples (4 frames of 256 at 48 kHz), so
    bytes/superframes is the superframe size and a quarter of it the frame.
    The encoder pads the last superframe, so the superframe count is
    ceil(samples/1024) or up to two more (measured: 7 of the 33 music tracks
    carry exactly one padding superframe).
    """
    needed = max(1, math.ceil(record["samples"] / 1024))
    for count in (needed, needed + 1, needed + 2):
        if record["bytes"] % count:
            continue
        superframe_bytes = record["bytes"] // count
        if superframe_bytes % 4 or not 4 <= superframe_bytes // 4 <= 2048:
            continue
        if count * 1024 < record["samples"]:
            continue
        return superframe_bytes, superframe_bytes // 4
    raise GateError(
        f"record at {record['offset']:#x}: {record['bytes']} B / {record['samples']} "
        f"samples has no ATRAC9 superframe geometry")


def at9_riff(payload, channels, rate, superframe_bytes, frame_bytes):
    """WAVEFORMATEXTENSIBLE + AT9 GUID + 12-byte extradata + one data chunk."""
    extradata = struct.pack("<I", 0) + at9_config(
        SAMPLE_RATE_INDEX[rate], BLOCK_CONFIG[channels], frame_bytes, 2) + b"\0\0\0\0"
    mask = {1: 0x4, 2: 0x3, 4: 0x33, 6: 0x3F, 8: 0x63F}[channels]
    fmt = struct.pack("<HHIIHH", 0xFFFE, channels, rate,
                      0, superframe_bytes, 0)
    fmt += struct.pack("<H", 22 + len(extradata))
    fmt += struct.pack("<HI", 0, mask)
    fmt += AT9_GUID + extradata
    body = b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt
    body += b"data" + struct.pack("<I", len(payload)) + payload
    return b"RIFF" + struct.pack("<I", len(body)) + body


def at9_riff_with_config(payload, channels, rate, block_align, cfg4):
    """Like at9_riff but with an explicit 4-byte config (used by --probe-0x42)."""
    extradata = struct.pack("<I", 0) + cfg4 + b"\0\0\0\0"
    mask = {1: 0x4, 2: 0x3, 4: 0x33, 6: 0x3F, 8: 0x63F}[channels]
    fmt = struct.pack("<HHIIHH", 0xFFFE, channels, rate, 0, block_align, 0)
    fmt += struct.pack("<H", 22 + len(extradata))
    fmt += struct.pack("<HI", 0, mask)
    fmt += AT9_GUID + extradata
    body = b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt
    body += b"data" + struct.pack("<I", len(payload)) + payload
    return b"RIFF" + struct.pack("<I", len(body)) + body


# --------------------------------------------------------------------------- #
# PS-ADPCM (resident banks)
# --------------------------------------------------------------------------- #


def ps_adpcm_decode(data, channels):
    """Decode 16-byte PS-ADPCM blocks (28 samples each).

    Stereo banks store the two channels as alternating blocks (L, R, L, R...);
    the declared per-channel sample count already assumes that layout.
    """
    blocks = len(data) // 16
    out = [[] for _ in range(channels)]
    prev1 = [0] * channels
    prev2 = [0] * channels
    for b in range(blocks):
        off = b * 16
        header = data[off]
        shift = header & 0x0F
        filt = (header >> 4) & 0x07
        c1, c2 = VAG_COEF[filt] if filt < len(VAG_COEF) else (0, 0)
        ch = b % channels
        s1, s2 = prev1[ch], prev2[ch]
        dst = out[ch]
        for i in range(2, 16):
            byte = data[off + i]
            for nib in (byte & 0x0F, byte >> 4):
                v = (nib << 12) if nib < 8 else ((nib - 16) << 12)
                if shift:
                    v >>= shift
                s = v + ((s1 * c1 + s2 * c2) >> 6)
                s = -32768 if s < -32768 else (32767 if s > 32767 else s)
                s2, s1 = s1, s
                dst.append(s)
        prev1[ch], prev2[ch] = s1, s2
    return out


def write_wav_s16(path, channels, rate, samples_per_channel):
    with wave.open(path, "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(rate)
        if channels == 1:
            w.writeframes(struct.pack("<%dh" % len(samples_per_channel[0]),
                                      *samples_per_channel[0]))
        else:
            n = min(len(c) for c in samples_per_channel)
            inter = bytearray()
            for i in range(n):
                for c in samples_per_channel:
                    inter += struct.pack("<h", c[i])
            w.writeframes(bytes(inter))


# --------------------------------------------------------------------------- #
# analysis
# --------------------------------------------------------------------------- #


def analyze_f32(raw_path, wav_path, channels, rate, expected_frames):
    import numpy as np

    data = np.fromfile(raw_path, dtype="<f4")
    if data.size % channels:
        raise GateError(f"{raw_path}: {data.size} floats is not a multiple of {channels} ch")
    frames = data.size // channels

    # The delivered artifact is the 16-bit WAV; check its header agrees with the
    # record and take the peak from it (ffmpeg clamps there, so peak <= 1.0 by
    # construction — the float peak above is the decoder's overlap-add overshoot).
    with wave.open(wav_path, "rb") as w:
        if w.getnchannels() != channels or w.getframerate() != rate:
            raise GateError(
                f"{wav_path}: {w.getnchannels()}ch/{w.getframerate()}Hz, expected "
                f"{channels}ch/{rate}Hz")
        if w.getnframes() != frames:
            raise GateError(f"{wav_path}: {w.getnframes()} frames, float stream has {frames}")
        pcm = np.frombuffer(w.readframes(frames), dtype="<i2")
    peak_s16 = float(np.abs(pcm).max()) / 32768.0 if pcm.size else 0.0
    finite = bool(np.isfinite(data).all())
    peak = float(np.abs(data).max()) if data.size else 0.0
    rms = float(np.sqrt((data.astype("f8") ** 2).mean())) if data.size else 0.0
    rms_db = 20.0 * math.log10(rms) if rms > 0 else -999.0
    clipped = float((np.abs(data) >= 1.0).mean()) if data.size else 0.0
    per_channel = [
        20.0 * math.log10(max(float(np.sqrt((data[c::channels].astype("f8") ** 2).mean())), 1e-12))
        for c in range(channels)
    ] if data.size else []
    return {
        "frames": int(frames),
        "duration_s": frames / rate,
        "expected_frames": int(expected_frames),
        "expected_duration_s": expected_frames / rate,
        "duration_error_ms": abs(frames - expected_frames) / rate * 1e3,
        "rms_dbfs": rms_db,
        "peak_float": peak,
        "peak_s16": peak_s16,
        "clipped_fraction": clipped,
        "finite": finite,
        "per_channel_rms_dbfs": per_channel,
    }


def check_analysis(stats, check):
    """Gate the measured numbers; returns (ok, problems, warnings).

    Hard rules are what makes the file a *decode*: finite samples, the declared
    duration within 50 ms, RMS above -60 dBFS, and a delivered s16 peak <= 1.0.
    The float peak beyond unity and the clipped fraction are warnings: all 33
    music masters overshoot unity (measured peak 0.50 .. 4.22) and the s16
    export clamps 0.0000 .. 0.54 % of samples, which is a mastering property,
    not a decode failure.  A float peak past 8.0 (+18 dBFS) is still treated as
    a broken decode.
    """
    problems = []
    warnings = []
    if not stats["finite"]:
        problems.append("non-finite samples (NaN/Inf)")
    if stats["duration_error_ms"] > DURATION_TOLERANCE_S * 1e3:
        problems.append(
            f"duration {stats['duration_s']:.3f}s is {stats['duration_error_ms']:.1f}ms "
            f"from the declared {stats['expected_duration_s']:.3f}s "
            f"(tolerance {DURATION_TOLERANCE_S * 1e3:.0f}ms)")
    if stats["rms_dbfs"] <= RMS_FLOOR_DBFS:
        problems.append(f"RMS {stats['rms_dbfs']:.1f} dBFS is at/below {RMS_FLOOR_DBFS:.0f}")
    if stats["peak_s16"] > 1.0:
        problems.append(f"s16 peak {stats['peak_s16']:.4f} > 1.0")
    if stats["peak_float"] > 8.0:
        problems.append(f"float peak {stats['peak_float']:.3f} is runaway")
    if stats["peak_float"] > 1.0:
        warnings.append(f"float peak {stats['peak_float']:.3f} (AT9 overlap-add overshoot)")
    if stats["clipped_fraction"] > 0.005:
        warnings.append(f"s16 export clamps {stats['clipped_fraction']:.2%} of samples")
    if not check:
        return True, [], []
    return (not problems), problems, warnings


# --------------------------------------------------------------------------- #
# carving
# --------------------------------------------------------------------------- #


def carve_music(root, out_dir, tracks, log, first=0):
    index_path = os.path.join(root, "sound", "shadow", "music.sxd1")
    payload_path = os.path.join(root, "sound", "shadow", "music.sxd2")
    index = read_bytes(index_path)
    payload = read_bytes(payload_path)

    header = parse_header(index, index_path)
    chunks = chunk_map(index, index_path)
    if "WAVE" not in chunks:
        raise GateError(f"{index_path}: no WAVE chunk")
    wave_off = chunks["WAVE"][0]
    records = [read_wave_record(index, rec, index_path)
               for rec, tag in table_records(index, wave_off, index_path)]
    if any(r["tag"] != 3 for r in records):
        raise GateError(f"{index_path}: WAVE tag != 3, record stride is not 76 B")
    if any(r["rate"] != 48000 for r in records):
        raise GateError(f"{index_path}: non-48 kHz record")

    # music.sxd1's +0x0C is the companion .sxd2 *file* size; the payload starts
    # at 0x60 (the SXDS header) inside it.
    payload_base = CHUNK_BASE
    companion_size = header["companion"]
    if companion_size == 0:
        raise GateError(f"{index_path}: no companion size at +0x0C")
    if len(payload) != companion_size:
        raise GateError(f"{payload_path}: size {len(payload):#x} disagrees with "
                        f"music.sxd1's companion field {companion_size:#x}")
    payload_size = len(payload) - payload_base
    partition = verify_music_partition(records, payload_size, payload_path)

    # REQD carries the human track names; record i is the i-th sound.
    names = []
    if "REQD" in chunks:
        for rec, tag in table_records(index, chunks["REQD"][0], index_path):
            name = index[rec + 0x20:rec + 0x60].split(b"\0")[0].decode("latin1")
            names.append(name)

    if first + tracks > len(records):
        raise GateError(f"{index_path}: only {len(records)} WAVE records")

    carved = []
    for i, rec in enumerate(records[first:first + tracks], start=first):
        label = names[i] if i < len(names) and names[i] else f"track{i:02d}"
        name = "music_%02d_%s" % (i, "".join(c if c.isalnum() else "_" for c in label))
        payload_slice = payload[rec["start"]:rec["start"] + rec["bytes"]]
        if len(payload_slice) != rec["bytes"]:
            raise GateError(f"{payload_path}: short read for {name}")
        superframe_bytes, frame_bytes = at9_geometry(rec)
        derived = at9_config(SAMPLE_RATE_INDEX[rec["rate"]],
                             BLOCK_CONFIG[rec["channels"]], frame_bytes, 2)
        # A 76-byte music record also carries the AT9 config word verbatim at
        # +0x40 (= 0xFE761DF0 on all 33): assert the derived word agrees.
        hint = struct.unpack_from("<I", index, rec["offset"] + 0x40)[0]
        if hint >> 24 == 0xFE and hint.to_bytes(4, "big") != derived:
            raise GateError(
                f"{index_path}: record {i} +0x40 = {hint:#010x} disagrees with the "
                f"derived AT9 config {derived.hex()}")
        riff = at9_riff(payload_slice, rec["channels"], rec["rate"],
                        superframe_bytes, frame_bytes)
        at9_path = os.path.join(out_dir, name + ".at9.wav")
        with open(at9_path, "wb") as fh:
            fh.write(riff)
        raw_path = os.path.join(out_dir, name + ".f32")
        wav_path = os.path.join(out_dir, name + ".wav")
        cmd = ["ffmpeg", "-hide_banner", "-v", "error", "-y", "-i", at9_path,
               "-map", "0:a", "-c:a", "pcm_f32le", "-f", "f32le", raw_path,
               "-c:a", "pcm_s16le", wav_path]
        proc = subprocess.run(cmd, capture_output=True, text=True)
        if proc.returncode != 0:
            raise GateError(f"ffmpeg failed for {name}: {proc.stderr.strip()}")
        stats = analyze_f32(raw_path, wav_path, rec["channels"], rec["rate"], rec["samples"])
        carved.append({
            "kind": "music",
            "name": name,
            "index": i,
            "payload_base": payload_base,
            "start": rec["start"],
            "bytes": rec["bytes"],
            "samples": rec["samples"],
            "rate": rec["rate"],
            "channels": rec["channels"],
            "fmt": fmt_word(rec["fmt"]),
            "at9_config": derived.hex(),
            "at9_config_record": "%08x" % hint,
            "superframe_bytes": superframe_bytes,
            "frame_bytes": frame_bytes,
            "wav": os.path.relpath(wav_path, out_dir),
            "stats": stats,
        })
        log(f"  music[{i:02d}] {label:<12} {rec['channels']}ch {rec['rate']} Hz  "
            f"{rec['bytes']} B  {stats['duration_s']:.3f} s  "
            f"RMS {stats['rms_dbfs']:.1f} dBFS")
    if not carved:
        raise GateError("no music track carved")
    return {"header": header, "chunks": [(m, o, s) for m, o, s in walk_chunks(index, index_path)],
            "partition": partition, "records": carved, "record_count": len(records)}


def fmt_word(value):
    return "0x%08X" % value


def carve_resident(root, out_dir, bank, log):
    path = os.path.join(root, "sound", "shadow", bank)
    buf = read_bytes(path)
    header = parse_header(buf, path)
    chunks = chunk_map(buf, path)
    for need in ("WAVE", "DATA"):
        if need not in chunks:
            raise GateError(f"{path}: no {need} chunk")
    wave_off, wave_size = chunks["WAVE"]
    data_off, data_size = chunks["DATA"]
    # DATA: 8-byte chunk header + 8 zero bytes, then the wave payload.  A
    # record's payload is at `record + 0x24 + start` (loader-derived; see the
    # FUN_00983220 decompilation in the S5 task-1 report).
    payload_base = data_off + 0x10
    payload_end = data_off + 8 + data_size

    table = table_records(buf, wave_off, path)
    records = [read_wave_record(buf, rec, path) for rec, _tag in table]
    if not records:
        raise GateError(f"{path}: empty WAVE table")
    # Record stride comes from the next record (variable 48/56/68/76 B in boss01).
    for k, r in enumerate(records):
        nxt = table[k + 1][0] if k + 1 < len(table) else wave_off + 8 + wave_size
        r["stride"] = nxt - r["offset"]
        # The 0x42 family carries an ATRAC9 config word somewhere at/after
        # +0x28 (56-byte records: +0x2C; 76-byte records: +0x40).  Find the
        # first 0xFE-prefixed dword rather than assuming a layout.
        r["at9_hint"] = None
        r["at9_hint_off"] = None
        for off in range(0x28, max(0x28, r["stride"] - 3), 4):
            value = struct.unpack_from("<I", buf, r["offset"] + off)[0]
            if value >> 24 == 0xFE:
                r["at9_hint"] = value
                r["at9_hint_off"] = off
                break
        # Loader formula: payload is relative to the WAVE record itself.
        r["payload"] = r["offset"] + 0x24 + r["start"]
        r["length"] = r["bytes"]
        if r["payload"] < payload_base or r["payload"] + r["length"] > payload_end:
            raise GateError(
                f"{path}: record at {r['offset']:#x} payload "
                f"{r['payload']:#x}+{r['length']} escapes DATA "
                f"[{payload_base:#x},{payload_end:#x})")
    if min(r["start"] for r in records) == 0:
        raise GateError(f"{path}: resident starts unexpectedly begin at 0")

    # The formula must tile the DATA payload.  Duplicate records may share a
    # range (boss01 has 17 duplicate groups); the merged union must be exactly
    # [payload_base, payload_end) with no gaps.
    spans = sorted(set((r["payload"], r["payload"] + r["length"]) for r in records))
    merged = []
    for start, end in spans:
        if merged and start <= merged[-1][1]:
            merged[-1] = (merged[-1][0], max(merged[-1][1], end))
        else:
            merged.append((start, end))
    if merged != [(payload_base, payload_end)]:
        raise GateError(
            f"{path}: record+0x24+start does not tile DATA: merged={[(hex(a), hex(b)) for a, b in merged[:4]]}"
            f"… ({len(merged)} segment(s))")

    by_fmt = {}
    for r in records:
        by_fmt.setdefault(r["fmt"], r)
    variants = []
    carved = []
    for fmt, rec in sorted(by_fmt.items()):
        codec = fmt & 0xFF
        channels = rec["channels"]
        base = rec["payload"]
        blob = buf[base:base + rec["length"]]
        name = "%s_fmt%08X_%dch_rec%04d" % (bank.split(".")[0], fmt, channels,
                                            rec["offset"])
        hint = rec["at9_hint"]
        record = {
            "kind": "resident",
            "record": "resident",
            "name": name,
            "fmt": fmt_word(fmt),
            "fmt_value": fmt,
            "channels": channels,
            "rate": rec["rate"],
            "start": rec["start"],
            "start_origin": "wave_record",
            "payload": base,
            "bytes": rec["length"],
            "samples": rec["samples"],
            "stride": rec["stride"],
            "at9_config_record": ("%08x" % hint) if hint else None,
            "at9_config_offset": rec["at9_hint_off"],
            "mapping": "wave_record_offset + 0x24 + start",
        }
        if codec == 0x21:
            record["codec"] = "ps_adpcm"
            pcm = ps_adpcm_decode(blob, channels)
            wav_path = os.path.join(out_dir, name + ".wav")
            write_wav_s16(wav_path, channels, rec["rate"], pcm)
            frames = min(len(c) for c in pcm)
            rms = 0.0
            peak = 0
            for c in pcm:
                for v in c:
                    rms += v * v
                    peak = max(peak, abs(v))
            total = frames * channels
            rms = math.sqrt(rms / total) / 32768.0 if total else 0.0
            stats = {
                "frames": frames,
                "duration_s": frames / rec["rate"],
                "expected_frames": rec["samples"],
                "expected_duration_s": rec["samples"] / rec["rate"],
                "duration_error_ms": abs(frames - rec["samples"]) / rec["rate"] * 1e3,
                "rms_dbfs": 20.0 * math.log10(rms) if rms > 0 else -999.0,
                "peak_float": peak / 32768.0,
                "peak_s16": peak / 32768.0,
                "clipped_fraction": 0.0,
                "finite": True,
                "per_channel_rms_dbfs": [],
            }
            record["stats"] = stats
            record["wav"] = os.path.relpath(wav_path, out_dir)
            record["status"] = "decoded"
            carved.append(record)
            log(f"  resident {fmt_word(fmt)} {channels}ch {rec['length']} B -> "
                f"{stats['duration_s']:.3f} s  RMS {stats['rms_dbfs']:.1f} dBFS")
            continue
        if codec == 0x42:
            if not hint:
                raise GateError(
                    f"{path}: 0x42 record at {rec['offset']:#x} has no ATRAC9 config word")
            bci = (hint >> 17) & 7
            frame_bytes = ((hint >> 5) & 0x7FF) + 1
            superframe = (hint >> 3) & 3
            expected_channels = {0: 1, 1: 2, 2: 2, 3: 6, 4: 8, 5: 4}.get(bci)
            if expected_channels != channels:
                raise GateError(
                    f"{path}: 0x42 record {rec['offset']:#x} config says {expected_channels}ch "
                    f"but the record says {channels}ch")
            record["codec"] = "atrac9"
            record["at9_frame_bytes"] = frame_bytes
            record["at9_superframe"] = superframe
            riff = at9_riff_with_config(blob, channels, rec["rate"],
                                        frame_bytes << superframe,
                                        hint.to_bytes(4, "big"))
            at9_path = os.path.join(out_dir, name + ".at9.wav")
            with open(at9_path, "wb") as fh:
                fh.write(riff)
            raw_path = os.path.join(out_dir, name + ".f32")
            wav_path = os.path.join(out_dir, name + ".wav")
            cmd = ["ffmpeg", "-hide_banner", "-v", "error", "-y", "-i", at9_path,
                   "-map", "0:a", "-c:a", "pcm_f32le", "-f", "f32le", raw_path,
                   "-c:a", "pcm_s16le", wav_path]
            proc = subprocess.run(cmd, capture_output=True, text=True)
            if proc.returncode != 0:
                raise GateError(f"ffmpeg failed for {name}: {proc.stderr.strip()}")
            stats = analyze_f32(raw_path, wav_path, channels, rec["rate"], rec["samples"])
            record["stats"] = stats
            record["wav"] = os.path.relpath(wav_path, out_dir)
            record["status"] = "decoded"
            carved.append(record)
            log(f"  resident {fmt_word(fmt)} {channels}ch {rec['length']} B -> "
                f"{stats['duration_s']:.3f} s  RMS {stats['rms_dbfs']:.1f} dBFS "
                f"(AT9 {frame_bytes}B/frame)")
            continue
        record["codec"] = "unresolved"
        record["status"] = "skipped: codec unresolved"
        record["reason"] = f"format 0x{codec:02X} has no decoder in this gate"
        carved.append(record)
        log(f"  resident {fmt_word(fmt)} {channels}ch {rec['length']} B: "
            f"skipped (format 0x{codec:02X})")
    for rec in carved:
        variants.append({"fmt": rec["fmt"], "fmt_value": rec["fmt_value"],
                         "status": rec["status"],
                         "channels": rec["channels"], "bytes": rec["bytes"],
                         "samples": rec["samples"],
                         "at9_config_record": rec.get("at9_config_record")})
    return {"header": header, "data_payload": payload_base, "data_end": payload_end,
            "payload_bytes": payload_end - payload_base,
            "mapping": "wave_record_offset + 0x24 + start",
            "record_count": len(records),
            "fmt_words": [fmt_word(f) for f in sorted(by_fmt)],
            "variants": variants, "records": carved}


def parse_fmt_list(text):
    """Parse '0x0142,0x0242' (or bare hex) into [int]."""
    out = []
    for tok in (text or "").replace(";", ",").split(","):
        tok = tok.strip()
        if not tok:
            continue
        try:
            value = int(tok, 16)
        except ValueError:
            raise GateError(f"bad format word {tok!r} (expected hex, e.g. 0x0142)")
        # Resident formats are 0x00FF____; accept the short 0x0142 form.
        out.append(0x00FF0000 | value if value <= 0xFFFF else value)
    return out


def probe_0x42(root, out_dir, bank, log):
    """Bounded identification attempt for the 0x42 family (note §5).

    Three hypotheses are tried against the first 0x42 record of each channel
    count, all with the record's declared byte range at the loader payload
    (`record + 0x24 + start`): (a) ATRAC9 with the config word the record
    itself carries, (b) PS-ADPCM at 16 B / 64 samples through ffmpeg's
    adpcm_psx, (c) the other ADPCM decoders whose block geometry can express
    64 samples per 16 bytes.  Nothing is written as a WAV: the probe reports
    and exits.
    """
    path = os.path.join(root, "sound", "shadow", bank)
    buf = read_bytes(path)
    chunks = chunk_map(buf, path)
    wave_off = chunks["WAVE"][0]
    data_off, data_size = chunks["DATA"]
    payload_base = data_off + 0x10
    table = table_records(buf, wave_off, path)
    records = [read_wave_record(buf, rec, path) for rec, _ in table]
    for k, r in enumerate(records):
        nxt = table[k + 1][0] if k + 1 < len(table) else len(buf)
        r["stride"] = nxt - r["offset"]
        r["at9_hint"] = None
        for off in range(0x28, max(0x28, r["stride"] - 3), 4):
            value = struct.unpack_from("<I", buf, r["offset"] + off)[0]
            if value >> 24 == 0xFE:
                r["at9_hint"] = value
                break
        r["payload"] = r["offset"] + 0x24 + r["start"]
    probe_dir = os.path.join(out_dir, "probe_0x42")
    os.makedirs(probe_dir, exist_ok=True)

    seen = set()
    for r in records:
        if (r["fmt"] & 0xFF) != 0x42 or r["channels"] in seen:
            continue
        seen.add(r["channels"])
        base = r["payload"]
        blob = buf[base:base + r["bytes"]]
        hint = r["at9_hint"]
        log(f"0x42 {fmt_word(r['fmt'])} {r['channels']}ch: {r['bytes']} B, "
            f"{r['samples']} samples, payload {base:#x}, "
            f"record config {('%08x' % hint) if hint else 'none'}")
        if not hint or hint >> 24 != 0xFE:
            log("  no ATRAC9 config word in the record; skipping the AT9 probe")
            continue
        sri = (hint >> 20) & 0xF
        bci = (hint >> 17) & 7
        frame_bytes = ((hint >> 5) & 0x7FF) + 1
        sf = (hint >> 3) & 3
        channels = {0: 1, 1: 2, 2: 2, 3: 6, 4: 8, 5: 4}[bci]
        rate = 48000
        log(f"  record config decodes to sri={sri} blockcfg={bci} ({channels}ch) "
            f"frame_bytes={frame_bytes} superframe={sf}")
        riff = at9_riff_with_config(blob, channels, rate, frame_bytes << sf, hint.to_bytes(4, "big"))
        p = os.path.join(probe_dir, "fmt%08X.at9.wav" % r["fmt"])
        with open(p, "wb") as fh:
            fh.write(riff)
        rc, frames, err = _ffmpeg_decode(p, [], channels)
        log(f"  (a) ATRAC9 @ payload {base:#x}: rc {rc}, "
            f"{frames} frames ({frames / rate:.2f} s of {r['samples'] / rate:.2f} s)"
            f" {err}")

        # (b) ffmpeg adpcm_psx via a VAG wrapper, 16-byte blocks -> 28 samples.
        vag = (b"VAGp" + struct.pack("<I", 0x20) + struct.pack(">I", len(blob))
               + struct.pack("<I", rate) + b"probe\0" + b"\0" * 16 + blob)
        p = os.path.join(probe_dir, "fmt%08X.vag" % r["fmt"])
        with open(p, "wb") as fh:
            fh.write(vag)
        rc, frames, err = _ffmpeg_decode(p, ["-f", "vag"], 1)
        log(f"  (b) adpcm_psx (.vag, 16 B -> 28 samples): rc {rc}, {frames} frames "
            f"({frames / rate:.2f} s of {r['samples'] / rate:.2f} s) {err}")

        # (c) the WAV-tagged ADPCM decoders at 64 samples per 16 bytes.
        for tag, name in ((0x0002, "adpcm_ms"), (0x0011, "adpcm_ima_wav"),
                          (0x0020, "adpcm_ct")):
            wav = _adpcm_wav(tag, 1, rate, 64, blob)
            p = os.path.join(probe_dir, "fmt%08X_%s.wav" % (r["fmt"], name))
            with open(p, "wb") as fh:
                fh.write(wav)
            rc, frames, err = _ffmpeg_decode(p, [], 1)
            log(f"  (c) {name} (tag {tag:#06x}, block 64): rc {rc}, {frames} frames "
                f"({frames / rate:.2f} s of {r['samples'] / rate:.2f} s) {err}")
        log("  verdict: ATRAC9 with the record's own config decodes the whole declared "
            "length at the loader payload (wave_record_offset + 0x24 + start); the "
            "ADPCM hypotheses only decode prefixes.  The family is resolved.")


def _adpcm_wav(tag, channels, rate, block_align, data):
    fmt = struct.pack("<HHIIHH", tag, channels, rate, rate * block_align // 64,
                      block_align, 0)
    body = b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt
    body += b"data" + struct.pack("<I", len(data)) + data
    return b"RIFF" + struct.pack("<I", len(body)) + body


def _ffmpeg_decode(path, extra_in, channels):
    raw = path + ".f32"
    cmd = (["ffmpeg", "-hide_banner", "-v", "error", "-y"] + extra_in
           + ["-i", path, "-c:a", "pcm_f32le", "-f", "f32le", raw])
    proc = subprocess.run(cmd, capture_output=True, text=True)
    frames = 0
    if os.path.exists(raw):
        frames = os.path.getsize(raw) // (4 * channels)
        os.remove(raw)
    first = ""
    if proc.returncode != 0:
        lines = [l.strip() for l in proc.stderr.splitlines() if l.strip()]
        first = "| " + (lines[0][:90] if lines else "")
    return proc.returncode, frames, first


# --------------------------------------------------------------------------- #
# main
# --------------------------------------------------------------------------- #


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", required=True, help="game root (contains sound/shadow)")
    ap.add_argument("--out", required=True, help="artifact directory")
    ap.add_argument("--check", action="store_true", help="gate the decoded PCM")
    ap.add_argument("--tracks", type=int, default=1,
                    help="music records to carve (default 1 = 'Prologue')")
    ap.add_argument("--first", type=int, default=0, help="first music record (0-based)")
    ap.add_argument("--verbose", action="store_true",
                    help="print per-file warnings")
    ap.add_argument("--no-resident", action="store_true",
                    help="skip the boss01 resident-bank carve")
    ap.add_argument("--bank", default="boss01.sxd", help="resident bank to carve")
    ap.add_argument("--require", default="",
                    help="comma-separated resident format words the gate MUST decode "
                         "(default: every variant found in the bank)")
    ap.add_argument("--allow-unresolved", default="",
                    help="comma-separated resident format words whose absence is an "
                         "accepted, printed gap")
    ap.add_argument("--probe-0x42", action="store_true", dest="probe42",
                    help="run the bounded 0x42 identification attempt and exit")
    args = ap.parse_args(argv)

    if shutil.which("ffmpeg") is None:
        print("GATE ERROR: ffmpeg (with the built-in atrac9 decoder) is not on PATH",
              file=sys.stderr)
        return 2
    os.makedirs(args.out, exist_ok=True)
    log = print

    log(f"SXDF/AT9 decode gate — root {args.root}")
    manifest = {"music": None, "resident": None, "checks": []}

    if args.probe42:
        probe_0x42(args.root, args.out, args.bank, log)
        return 0

    music = carve_music(args.root, args.out, args.tracks, log, args.first)
    manifest["music"] = music
    log(f"  music.sxd1: {music['record_count']} WAVE records, "
        f"partition {music['partition']['payload_bytes']} B, gaps {music['partition']['gaps']}")

    if not args.no_resident:
        resident = carve_resident(args.root, args.out, args.bank, log)
        manifest["resident"] = resident
        log(f"  {args.bank}: {resident['record_count']} WAVE records, "
            f"{len(resident['fmt_words'])} distinct format words, "
            f"payload {resident['payload_bytes']} B "
            f"(mapping: {resident['mapping']})")

    failures = 0
    skipped = 0
    gaps = []
    for group in ("music", "resident"):
        if not manifest[group]:
            continue
        for rec in manifest[group]["records"]:
            if "stats" not in rec:
                skipped += 1
                gaps.append(rec.get("fmt_value"))
                manifest["checks"].append({"name": rec["name"], "ok": True,
                                           "skipped": True, "problems": [],
                                           "reason": rec.get("status", "")})
                continue
            ok, problems, warnings = check_analysis(rec["stats"], args.check)
            manifest["checks"].append({"name": rec["name"], "ok": ok,
                                       "problems": problems, "warnings": warnings})
            if not ok:
                failures += 1
                log(f"  FAIL {rec['name']}: {'; '.join(problems)}")
            elif warnings and args.verbose:
                log(f"  warn {rec['name']}: {'; '.join(warnings)}")

    manifest_path = os.path.join(args.out, "manifest.json")
    with open(manifest_path, "w") as fh:
        json.dump(manifest, fh, indent=2)
    log(f"  manifest -> {os.path.relpath(manifest_path, args.out)}")

    # --- variant honesty --------------------------------------------------
    # Every resident format word found in the bank is required by default.  A
    # variant that is skipped fails the gate unless it is named in
    # --allow-unresolved, and even then the gate prints what that costs.
    explicit_required = parse_fmt_list(args.require)
    required = explicit_required or None
    allowed = parse_fmt_list(args.allow_unresolved)
    decoded = []
    if manifest["resident"]:
        decoded = [v["fmt_value"] for v in manifest["resident"]["variants"]
                   if v["status"] == "decoded"]
        if required is None:
            required = [v["fmt_value"] for v in manifest["resident"]["variants"]]
    required = required or []
    # Only an explicit --require clashes with --allow-unresolved; the default
    # "require every variant" is exactly what the allowlist is allowed to carve out.
    overlap = [f for f in explicit_required if f in allowed]
    if overlap:
        log("GATE ERROR: %s both required and allowed-unresolved"
            % ", ".join(fmt_word(f) for f in overlap))
        return 2
    unacceptable = [f for f in gaps if f not in allowed]
    missing = [f for f in required if f not in decoded and f not in allowed]
    unacceptable = [f for f in unacceptable if f not in missing]
    manifest["variants"] = {"required": [fmt_word(f) for f in required],
                            "decoded": [fmt_word(f) for f in decoded],
                            "allowed_unresolved": [fmt_word(f) for f in allowed],
                            "unresolved": [fmt_word(f) for f in gaps]}
    if args.check:
        if missing:
            for f in missing:
                log(f"  FAIL required variant {fmt_word(f)} was not decoded and is not "
                    "in --allow-unresolved")
            failures += len(missing)
        if unacceptable:
            for f in unacceptable:
                log(f"  FAIL variant {fmt_word(f)} skipped but not named in "
                    "--allow-unresolved")
            failures += len(unacceptable)

    if args.check and gaps and not decoded and not failures:
        log("  BLOCKED: no resident family is validated — the accepted gaps cover "
            "every variant, so there is no decodable family for Task 1 to attribute "
            "the curated cues to.")
        log("  POLICY: the milestone stops at Task 1 until a resident family decodes "
            "(note §7).")
        return 3
    if failures:
        log(f"GATE FAILED: {failures} problem(s)")
        if missing or unacceptable:
            log("  the gate is a hard stop: decode the variant or accept the gap "
                "explicitly with --allow-unresolved")
        return 1
    if gaps:
        log("  ALLOWED GAPS: %s (%d resident variant(s) not decoded) — see "
            "docs/notes/s5-audio-recon.md §5" % (", ".join(fmt_word(f) for f in gaps), len(gaps)))
        log("  POLICY: the curated cues must come from the decoded families (%s); "
            "if Task 1 attributes a core cue to %s the milestone stops at Task 1."
            % (", ".join(fmt_word(f) for f in decoded) or "none",
               ", ".join(fmt_word(f) for f in gaps)))
    if args.check:
        log(f"GATE PASSED: music 48 kHz non-silent; resident variants decoded "
            f"{', '.join(fmt_word(f) for f in decoded) or 'none'}"
            + (f"; accepted gaps {', '.join(fmt_word(f) for f in gaps)}" if gaps else ""))
    else:
        log("carve complete (run with --check to gate)")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except GateError as exc:
        print(f"GATE ERROR: {exc}", file=sys.stderr)
        sys.exit(2)
