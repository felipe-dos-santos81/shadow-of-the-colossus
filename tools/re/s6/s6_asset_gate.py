#!/usr/bin/env python3
"""S6 asset/death gate — prove Valus's death bundle, corpse and tendril assets before S6 code.

Read-only on both trees. `--root` is the extracted game tree, `--out` is the converted
library (the `out/library` symlink); nothing is written.

What is gated (and where the evidence lives, `docs/notes/s6-recon.md`):

Death bundle — `cp3shadow/anim/`
  `minotaur_a_death_front.anb`      24,732 B, "xff\\0", 94 f @ 15.0806 fps = 6.233 s, 58 tracks
  `minotaur_a_death_front_loop.anb`  3,532 B, 1 f (the shipped dead-hold pose)
  `objanim_minotaur_a_death_front.anb` 4,808 B, object track type 29
  `camera_minoa_death_front_camera_01/02.anb` 844 / 2,244 B, track type 36 (undecoded)
  The app's copy of the motion is the baked glTF animation in
  `models/shadow/characters/colossus_01_minotaur_a/_cmn/c01_minotaur_a.glb`: 94 keys,
  6.1669 s, named `minotaur_a_death_front`; `c01_minotaur_a.clips.json` records
  `frames: 94`, `fps: 15.08056640625`, `tracks: 58`.

  The gate hard-requires the *baked* clip (the artifact the viewer plays) and checks the
  source `.anb` bytes against the extraction manifest hashes.  A source that no longer
  carries the `xff\\0` magic is reported as a **source warning**: the game tree is
  read-only for this project, and the original bytes are recoverable from the paks
  (see the note).  A missing library asset is always a hard failure.

Dead body — `models/shadow/characters/colossus_01_minotaur_a_deadbody/_cmn/`
  `c01_minotaur_a_dead_body.glb` 11,924,836 B, 6 meshes / 6 materials / 0 skins, all
  materials `colossus_01_*_dead`; `c01_minotaur_a_dead_dirtmound.glb` 254,948 B.

Tendril textures — `out/library/textures/`
  `vfx/common/smoke/_ps4/8x8_wispysmoke02_1024_white_pfx.png`   1024x1024
  `vfx/common/smoke/_ps4/8x8_smoke_wispy_sharp_1024_pfx.png`   1024x1024
  `vfx/common/smokeman/_ps4/2x2_smoke_tendrils_01_pfx.png`       512x512
  `shadow/characters/common/_ps4/death_tentacle_veins.png`      1024x256 (the ribbon)

`.cprt` — `particles/shadow/colossus/death_tentacle/_ps4/death_tentacle.cprt`
  2,376 B.  Header u32[5] = {0, 3, 8, 1, 0x64} (3 emitters, data base 0x64).
  String block from 0x80; per emitter a `death_tentacle:<name>` marker, three
  `emitter_<i>_{emit,spawn,update}.csdr` references and one `.cptx` reference:
    main_wisp  -> textures/vfx/common/smoke/_ps4/8x8_wispysmoke02_1024_white_pfx.cptx
    smoke      -> textures/vfx/common/smoke/_ps4/8x8_smoke_wispy_sharp_1024_pfx.cptx
    veins      -> textures/vfx/common/smokeman/_ps4/2x2_smoke_tendrils_01_pfx.cptx
  Footer: "TRAPL" + u32 0 + u32 (file size - 12) at the last 16 bytes.

Usage
  python3 tools/re/s6/s6_asset_gate.py --root "$SOTC_GAME_ROOT" --out out/library --check

Exit codes: 0 = every required asset present and valid; 4 = a required asset is missing
or invalid (each one is named); 2 = configuration/IO error (bad root/out, unreadable
file that is not an asset verdict).  This is a hard gate: S6 code starts after it passes.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
import sys

XFF_MAGIC = b"\x78\x66\x66\x00"  # "xff\0" — .anb / .skb / .nmo / .nmb / .clb
PNG_MAGIC = b"\x89PNG\r\n\x1a\n"

# The five death-bundle clips: (path under the game root, size, sha256 of the shipped file).
# Hashes from SOTC-extract/logs/game.sha256; the bytes were re-read from pak3.psarc /
# cachedFiles.psarc on 2026-10-22 while writing docs/notes/s6-recon.md.
DEATH_SOURCES = [
    ("cp3shadow/anim/minotaur_a_death_front.anb", 24732,
     "0db1d990ba55687d6539ce61b39274684c29c0d281a6732cb7392cb07edf6c2b"),
    ("cp3shadow/anim/minotaur_a_death_front_loop.anb", 3532,
     "a7639824b3e004a6207113d358072914df2a26706bf09fc609a5b56b3fb87a3f"),
    ("cp3shadow/anim/objanim_minotaur_a_death_front.anb", 4808,
     "f5daae1a9450a82bdc1aa33b6d6e7ef6ee607a49b5729bd43f127e7007644351"),
    ("cp3shadow/anim/camera_minoa_death_front_camera_01.anb", 844,
     "e15eecc7375751ecc2ece8998dbacba8a29599468b09939d19402bfeb71f3a51"),
    ("cp3shadow/anim/camera_minoa_death_front_camera_02.anb", 2244,
     "1a57e540eedf629c8b8b5cb4452b266755ad0544e00a9be999fd76541fe51146"),
]

# The baked motion the viewer plays (the model glb carries it) and its clip record.
CLIP_MODEL = "models/shadow/characters/colossus_01_minotaur_a/_cmn/c01_minotaur_a.glb"
CLIP_RECORD = "models/shadow/characters/colossus_01_minotaur_a/_cmn/c01_minotaur_a.clips.json"
CLIP_NAME = "minotaur_a_death_front"
CLIP_FRAMES = 94
CLIP_FPS = 15.08056640625
CLIP_TRACKS = 58
CLIP_SECONDS = (CLIP_FRAMES - 1) / CLIP_FPS  # 6.1669 s: 94 keys span 93 intervals

DEAD_BODY = "models/shadow/characters/colossus_01_minotaur_a_deadbody/_cmn/c01_minotaur_a_dead_body.glb"
DEAD_BODY_MESHES = 6
DEAD_BODY_MATERIALS = 6
DIRTMOUND = "models/shadow/characters/colossus_01_minotaur_a_deadbody/_cmn/c01_minotaur_a_dead_dirtmound.glb"

# The `death_tentacle.cprt` emitter order -> converted atlas under <out>/textures/.
# (emitter name, cptx basename, library PNG, expected size)
CPRT = "particles/shadow/colossus/death_tentacle/_ps4/death_tentacle.cprt"
CPRT_SIZE = 2376
CPRT_HEADER = (0, 3, 8, 1, 100)
CPRT_EMITTERS = [
    ("main_wisp", "8x8_wispysmoke02_1024_white_pfx.cptx",
     "textures/vfx/common/smoke/_ps4/8x8_wispysmoke02_1024_white_pfx.png", (1024, 1024)),
    ("smoke", "8x8_smoke_wispy_sharp_1024_pfx.cptx",
     "textures/vfx/common/smoke/_ps4/8x8_smoke_wispy_sharp_1024_pfx.png", (1024, 1024)),
    ("veins", "2x2_smoke_tendrils_01_pfx.cptx",
     "textures/vfx/common/smokeman/_ps4/2x2_smoke_tendrils_01_pfx.png", (512, 512)),
]

# The ribbon texture is not a .cprt texture: it is the dead-body material's veil layer.
VEINS_PNG = ("textures/shadow/characters/common/_ps4/death_tentacle_veins.png", (1024, 256))


class GateError(Exception):
    """A configuration/IO problem, not an asset verdict (exit 2)."""


# --------------------------------------------------------------------------- helpers


def read_bytes(path):
    try:
        with open(path, "rb") as fh:
            return fh.read()
    except OSError as exc:
        raise GateError("cannot read %s: %s" % (path, exc))


def png_size(path):
    """Validate the PNG signature + IHDR and return (width, height)."""
    data = read_bytes(path)
    if len(data) < 24 or not data.startswith(PNG_MAGIC):
        raise ValueError("not a PNG (signature missing)")
    width, height = struct.unpack(">II", data[16:24])
    if width <= 0 or height <= 0:
        raise ValueError("PNG declares %dx%d" % (width, height))
    return width, height


def glb_summary(path):
    """Parse the GLB container's JSON chunk: meshes/materials/skins/animations."""
    data = read_bytes(path)
    if len(data) < 20 or data[:4] != b"glTF":
        raise ValueError("not a GLB (magic missing)")
    version, declared = struct.unpack_from("<II", data, 4)
    if version != 2:
        raise ValueError("GLB version %d, expected 2" % version)
    if declared != len(data):
        raise ValueError("GLB header says %d B, file is %d B" % (declared, len(data)))
    chunk_len, chunk_type = struct.unpack_from("<I4s", data, 12)
    if chunk_type != b"JSON":
        raise ValueError("first GLB chunk is %r, not JSON" % chunk_type)
    doc = json.loads(data[20:20 + chunk_len])
    accessors = doc.get("accessors", [])
    animations = {}
    for anim in doc.get("animations", []):
        counts, ends = [], []
        for sampler in anim.get("samplers", []):
            accessor = accessors[sampler["input"]]
            counts.append(accessor.get("count", 0))
            ends.append(accessor.get("max", [0])[0])
        animations[anim.get("name", "")] = {
            "keys": max(counts) if counts else 0,
            "seconds": max(ends) if ends else 0.0,
        }
    return {
        "meshes": len(doc.get("meshes", [])),
        "materials": [m.get("name", "") for m in doc.get("materials", [])],
        "skins": len(doc.get("skins", [])),
        "animations": animations,
        "images": [i.get("uri", "") for i in doc.get("images", [])],
    }


def cprt_strings(data):
    """Every printable NUL-terminated run (>= 4 chars) as (offset, text)."""
    runs, i = [], 0
    while i < len(data):
        end = data.find(b"\x00", i)
        if end < 0:
            break
        run = data[i:end]
        if len(run) >= 4 and all(32 <= byte <= 126 for byte in run):
            runs.append((i, run.decode("ascii")))
        i = end + 1
    return runs


def parse_cprt(path):
    """Parse the emitter records: header, footer and (name -> csdr[3] + cptx) per emitter."""
    data = read_bytes(path)
    if len(data) != CPRT_SIZE:
        raise ValueError("size %d B, expected %d B" % (len(data), CPRT_SIZE))
    header = struct.unpack_from("<5I", data, 0)
    if header != CPRT_HEADER:
        raise ValueError("header %s, expected %s" % (list(header), list(CPRT_HEADER)))
    if data[-12:-7] != b"TRAPL":
        raise ValueError("footer magic missing (got %r)" % data[-12:-7])
    if struct.unpack_from("<I", data, len(data) - 4)[0] != len(data) - 12:
        raise ValueError("footer size word is not file size - 12")

    runs = cprt_strings(data)
    # Search the marker in the raw bytes: it is not NUL-aligned (it follows the emitter's
    # binary record, whose last float byte can be printable or >= 0x80), so a whole-run
    # ASCII filter would drop the `veins` marker.
    markers, cursor = [], 0
    while True:
        start = data.find(b"death_tentacle:", cursor)
        if start < 0:
            break
        end = data.find(b"\x00", start)
        markers.append((start, data[start + len(b"death_tentacle:"):end].decode("ascii")))
        cursor = end + 1
    if [name for _, name in markers] != [name for name, _, _, _ in CPRT_EMITTERS]:
        raise ValueError("emitter markers %s, expected %s"
                         % ([name for _, name in markers], [e[0] for e in CPRT_EMITTERS]))

    emitters = []
    for index, (off, name) in enumerate(markers):
        end = markers[index + 1][0] if index + 1 < len(markers) else len(data)
        refs = [text for text_off, text in runs if off < text_off < end]
        csdr = [text for text in refs if text.endswith(".csdr")]
        cptx = [text for text in refs if text.endswith(".cptx")]
        if len(csdr) != 3:
            raise ValueError("%s: %d .csdr reference(s), expected 3" % (name, len(csdr)))
        if len(cptx) != 1:
            raise ValueError("%s: %d .cptx reference(s), expected 1" % (name, len(cptx)))
        expected_csdr = ["emitter_%d_%s.csdr" % (index, part) for part in ("emit", "spawn", "update")]
        if [os.path.basename(ref) for ref in csdr] != expected_csdr:
            raise ValueError("%s: csdr refs %s, expected %s"
                             % (name, [os.path.basename(r) for r in csdr], expected_csdr))
        emitters.append({
            "name": name,
            "csdr": csdr,
            "cptx": cptx[0],
            "cptx_name": os.path.basename(cptx[0]),
        })
    return {"header": header, "emitters": emitters}


# --------------------------------------------------------------------------- checks


class Report:
    def __init__(self):
        self.ok = 0
        self.failures = []   # (asset, detail)
        self.warnings = []   # (asset, detail)

    def good(self, section, asset, detail):
        self.ok += 1
        print("  ok   %-8s %s  %s" % (section, asset, detail))

    def bad(self, section, asset, detail):
        self.failures.append((asset, detail))
        print("  FAIL %-8s %s  %s" % (section, asset, detail))

    def warn(self, section, asset, detail):
        self.warnings.append((asset, detail))
        print("  warn %-8s %s  %s" % (section, asset, detail))


def library_path(out, rel):
    return os.path.join(out, rel.replace("/", os.sep))


def check_sources(root, report):
    print("clips (source)")
    for rel, expected_size, expected_sha in DEATH_SOURCES:
        path = os.path.join(root, rel.replace("/", os.sep))
        name = os.path.basename(rel)
        if not os.path.isfile(path):
            report.bad("source", name, "missing: %s" % path)
            continue
        data = read_bytes(path)
        digest = hashlib.sha256(data).hexdigest()
        if data[:4] == XFF_MAGIC and len(data) == expected_size and digest == expected_sha:
            report.good("source", name, "%d B sha256 %s" % (len(data), digest[:16]))
        else:
            detail = ("CORRUPTED: %d B, head %s, sha256 %s — expected %d B sha256 %s; "
                      "the game-tree copy is not this clip (see docs/notes/s6-recon.md §2.1)"
                      % (len(data), data[:4].hex(), digest[:16], expected_size, expected_sha[:16]))
            if data[:4] != XFF_MAGIC:
                report.warn("source", name, detail)
            else:
                report.bad("source", name, detail)


def check_clip_model(out, report):
    print("clips (library)")
    record_path = library_path(out, CLIP_RECORD)
    if not os.path.isfile(record_path):
        report.bad("clip", CLIP_RECORD, "missing: %s" % record_path)
    else:
        try:
            record = json.loads(read_bytes(record_path).decode("utf-8"))
            clips = {clip.get("name", ""): clip for clip in record.get("clips", [])}
            clip = clips.get(CLIP_NAME)
            if clip is None:
                raise ValueError("clip '%s' not in the record (has %s)"
                                 % (CLIP_NAME, ", ".join(sorted(clips))))
            if clip.get("frames") != CLIP_FRAMES:
                raise ValueError("frames %r, expected %d" % (clip.get("frames"), CLIP_FRAMES))
            if abs(float(clip.get("fps", 0)) - CLIP_FPS) > 1e-6:
                raise ValueError("fps %r, expected %r" % (clip.get("fps"), CLIP_FPS))
            if clip.get("tracks") != CLIP_TRACKS:
                raise ValueError("tracks %r, expected %d" % (clip.get("tracks"), CLIP_TRACKS))
            report.good("clip", CLIP_RECORD,
                        "%s frames=%d fps=%.4f tracks=%d"
                        % (CLIP_NAME, clip["frames"], clip["fps"], clip["tracks"]))
        except (ValueError, KeyError, TypeError) as exc:
            report.bad("clip", CLIP_RECORD, str(exc))

    glb_path = library_path(out, CLIP_MODEL)
    if not os.path.isfile(glb_path):
        report.bad("clip", CLIP_MODEL, "missing: %s" % glb_path)
        return
    try:
        summary = glb_summary(glb_path)
        anim = summary["animations"].get(CLIP_NAME)
        if anim is None:
            raise ValueError("animation '%s' not baked (has %s)"
                             % (CLIP_NAME, ", ".join(sorted(summary["animations"]))))
        if anim["keys"] != CLIP_FRAMES:
            raise ValueError("%d keys, expected %d" % (anim["keys"], CLIP_FRAMES))
        if abs(anim["seconds"] - CLIP_SECONDS) > 1e-3:
            raise ValueError("%.4f s, expected %.4f s" % (anim["seconds"], CLIP_SECONDS))
        report.good("clip", os.path.basename(CLIP_MODEL),
                    "animation %s %d keys, %.4f s" % (CLIP_NAME, anim["keys"], anim["seconds"]))
    except ValueError as exc:
        report.bad("clip", os.path.basename(CLIP_MODEL), str(exc))


def check_corpse(out, report):
    print("corpse")
    body_path = library_path(out, DEAD_BODY)
    if not os.path.isfile(body_path):
        report.bad("corpse", DEAD_BODY, "missing: %s" % body_path)
    else:
        try:
            summary = glb_summary(body_path)
            problems = []
            if summary["meshes"] != DEAD_BODY_MESHES:
                problems.append("%d meshes, expected %d" % (summary["meshes"], DEAD_BODY_MESHES))
            if len(summary["materials"]) != DEAD_BODY_MATERIALS:
                problems.append("%d materials, expected %d"
                                % (len(summary["materials"]), DEAD_BODY_MATERIALS))
            if summary["skins"] != 0:
                problems.append("%d skins, expected 0 (static corpse)" % summary["skins"])
            not_dead = [name for name in summary["materials"] if not name.endswith("_dead")]
            if not_dead:
                problems.append("non-dead material(s): %s" % ", ".join(not_dead))
            if problems:
                raise ValueError("; ".join(problems))
            report.good("corpse", os.path.basename(DEAD_BODY),
                        "%d meshes, %d materials (%s), 0 skins"
                        % (summary["meshes"], len(summary["materials"]),
                           ", ".join(summary["materials"])))
        except ValueError as exc:
            report.bad("corpse", os.path.basename(DEAD_BODY), str(exc))

    mound_path = library_path(out, DIRTMOUND)
    if not os.path.isfile(mound_path):
        report.bad("corpse", DIRTMOUND, "missing: %s" % mound_path)
    else:
        try:
            summary = glb_summary(mound_path)
            if summary["meshes"] < 1:
                raise ValueError("no meshes")
            report.good("corpse", os.path.basename(DIRTMOUND),
                        "%d mesh(es), %d material(s)" % (summary["meshes"], len(summary["materials"])))
        except ValueError as exc:
            report.bad("corpse", os.path.basename(DIRTMOUND), str(exc))


def check_cprt(root, out, report):
    print("cprt")
    path = os.path.join(root, CPRT.replace("/", os.sep))
    if not os.path.isfile(path):
        report.bad("cprt", os.path.basename(CPRT), "missing: %s" % path)
        return None
    try:
        parsed = parse_cprt(path)
        report.good("cprt", os.path.basename(CPRT),
                    "header %s, 3 emitters" % ",".join(str(v) for v in parsed["header"]))
    except (ValueError, struct.error) as exc:
        report.bad("cprt", os.path.basename(CPRT), str(exc))
        return None

    for index, (expected_name, expected_cptx, _, _) in enumerate(CPRT_EMITTERS):
        emitter = parsed["emitters"][index]
        problems = []
        if emitter["name"] != expected_name:
            problems.append("named %r, expected %r" % (emitter["name"], expected_name))
        if emitter["cptx_name"] != expected_cptx:
            problems.append("cptx %r, expected %r" % (emitter["cptx_name"], expected_cptx))
        for ref in emitter["csdr"] + [emitter["cptx"]]:
            game_rel = ref[2:] if ref.startswith("$/") else ref
            if not os.path.isfile(os.path.join(root, game_rel.replace("/", os.sep))):
                problems.append("game-tree reference missing: %s" % game_rel)
        if problems:
            report.bad("cprt", emitter["name"], "; ".join(problems))
        else:
            report.good("cprt", emitter["name"],
                        "3 csdr (emitter_%d_*) + %s" % (index, emitter["cptx"]))
    return parsed


def check_textures(out, report, parsed):
    print("textures")
    cptx_by_name = {}
    if parsed is not None:
        cptx_by_name = {emitter["name"]: emitter["cptx_name"] for emitter in parsed["emitters"]}

    for name, expected_cptx, rel, expected_size in CPRT_EMITTERS:
        path = library_path(out, rel)
        label = os.path.basename(rel)
        if name in cptx_by_name and cptx_by_name[name] != expected_cptx:
            report.bad("texture", label,
                       "the cprt binds %s, expected %s" % (cptx_by_name[name], expected_cptx))
            continue
        if not os.path.isfile(path):
            report.bad("texture", label, "missing: %s" % path)
            continue
        try:
            size = png_size(path)
            if size != expected_size:
                raise ValueError("%dx%d, expected %dx%d" % (size + expected_size))
            report.good("texture", label,
                        "%dx%d, %d B" % (size[0], size[1], os.path.getsize(path)))
        except ValueError as exc:
            report.bad("texture", label, str(exc))

    rel, expected_size = VEINS_PNG
    path = library_path(out, rel)
    label = os.path.basename(rel)
    if not os.path.isfile(path):
        report.bad("texture", label, "missing: %s" % path)
    else:
        try:
            size = png_size(path)
            if size != expected_size:
                raise ValueError("%dx%d, expected %dx%d" % (size + expected_size))
            report.good("texture", label,
                        "%dx%d, %d B" % (size[0], size[1], os.path.getsize(path)))
        except ValueError as exc:
            report.bad("texture", label, str(exc))


# --------------------------------------------------------------------------- main


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", required=True, help="extracted game tree (SOTC_GAME_ROOT)")
    ap.add_argument("--out", required=True, help="converted library root (out/library)")
    ap.add_argument("--check", action="store_true",
                    help="gate the assets (exit 4 on a missing/invalid one)")
    args = ap.parse_args(argv)

    if not os.path.isdir(args.root):
        raise GateError("game root not found: %s" % args.root)
    if not os.path.isdir(args.out):
        raise GateError("library root not found: %s (convert and symlink out/library first)"
                        % args.out)

    print("S6 asset/death gate — root %s" % args.root)
    print("                     out  %s" % os.path.abspath(args.out))
    report = Report()

    check_sources(args.root, report)
    check_clip_model(args.out, report)
    check_corpse(args.out, report)
    parsed = check_cprt(args.root, args.out, report)
    check_textures(args.out, report, parsed)

    total = report.ok + len(report.failures)
    print()
    print("checked %d asset(s): %d ok, %d failed, %d source warning(s)"
          % (total, report.ok, len(report.failures), len(report.warnings)))
    if report.warnings:
        print("  source warnings: the game-tree copies listed above are not the shipped clips;")
        print("  the library/baked assets are unaffected (docs/notes/s6-recon.md §2.1)")
    if not args.check:
        print("inventory only — run with --check to gate")
        return 0
    if report.failures:
        for asset, detail in report.failures:
            print("  missing/invalid: %s — %s" % (asset, detail))
        print("S6 ASSET GATE FAILED — %d required asset(s) missing or invalid"
              % len(report.failures))
        return 4
    print("S6 ASSET GATE PASSED")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except GateError as exc:
        print("S6 ASSET GATE ERROR: %s" % exc, file=sys.stderr)
        sys.exit(2)
