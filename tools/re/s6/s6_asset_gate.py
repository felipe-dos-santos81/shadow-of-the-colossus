#!/usr/bin/env python3
"""S6 asset/death gate — prove Valus's death bundle, corpse and tendril assets before S6 code.

Read-only on both trees. `--root` is the extracted game tree, `--out` is the converted
library (the `out/library` symlink); nothing is written.

What is gated (and where the evidence lives, `docs/notes/s6-recon.md`):

Death bundle — `cp3shadow/anim/`
  `minotaur_a_death_front.anb`      24,732 B, "xff\\0", 94 f @ 15.0806 fps, 58 tracks
  `minotaur_a_death_front_loop.anb`  3,532 B, 1 f (the shipped dead-hold pose)
  `objanim_minotaur_a_death_front.anb` 4,808 B, object track type 29
  `camera_minoa_death_front_camera_01/02.anb` 844 / 2,244 B, track type 36 (undecoded)
  The app's copy of the motion is the baked glTF animation in
  `models/shadow/characters/colossus_01_minotaur_a/_cmn/c01_minotaur_a.glb`: 94 keys,
  6.1669 s, named `minotaur_a_death_front`; `c01_minotaur_a.clips.json` records
  `frames: 94`, `fps: 15.08056640625`, `tracks: 58`.

  The gate hard-requires the *baked* clip (the artifact the viewer plays) and checks the
  source `.anb` bytes against the extraction manifest hashes.  Exactly one exception is a
  **source warning**: the known 2026-10-22 clobber (246,449 B, sha256 a4d9c3b0…), because
  the game tree is read-only for this project and the original bytes are recoverable from
  the paks (see the note §2.1).  Any other source mismatch, and every missing library
  asset, is a hard failure.

Dead body — `models/shadow/characters/colossus_01_minotaur_a_deadbody/_cmn/`
  `c01_minotaur_a_dead_body.glb` 11,925,120 B, 6 meshes / 6 materials / 0 skins, the six
  materials exactly `colossus_01_{waist,back,face,arms,chest,legs}_dead`, each carrying
  `death_tentacle_veins.png` as its `emissiveTexture` (the S6 corpse's black-vein veil, D9);
  `c01_minotaur_a_dead_dirtmound.glb` 254,948 B.

Tendril textures — `out/library/textures/`
  `vfx/common/smoke/_ps4/8x8_wispysmoke02_1024_white_pfx.png`   1024x1024
  `vfx/common/smoke/_ps4/8x8_smoke_wispy_sharp_1024_pfx.png`   1024x1024
  `vfx/common/smokeman/_ps4/2x2_smoke_tendrils_01_pfx.png`       512x512
  `shadow/characters/common/_ps4/death_tentacle_veins.png`      1024x256 (the ribbon)
  Each is pinned by dimensions *and* SHA-256, so a blank/wrong atlas of the right size
  fails (the converter's PNG output is deterministic for these containers).

`.cprt` — `particles/shadow/colossus/death_tentacle/_ps4/death_tentacle.cprt`
  2,376 B.  Header u32[5] = {0, 3, 8, 1, 0x64} (3 emitters, data base 0x64).
  String block from 0x80; per emitter a `death_tentacle:<name>` marker, three
  `emitter_<i>_{emit,spawn,update}.csdr` references and one `.cptx` reference:
    main_wisp  -> textures/vfx/common/smoke/_ps4/8x8_wispysmoke02_1024_white_pfx.cptx
    smoke      -> textures/vfx/common/smoke/_ps4/8x8_smoke_wispy_sharp_1024_pfx.cptx
    veins      -> textures/vfx/common/smokeman/_ps4/2x2_smoke_tendrils_01_pfx.cptx
  Footer (last 12 bytes, from 0x93c): "TRAPL" + 3 pad bytes + u32 (file size - 12).

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
import posixpath
import struct
import sys

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

# The known 2026-10-22 corruption: a 246,449-byte `player_a_stand` JSON dump was written
# over four of the five death-bundle clips (see docs/notes/s6-recon.md §2.1). That exact
# incident is a warning; any other mismatch is a hard failure.
KNOWN_CLOBBER_BYTES = 246449
KNOWN_CLOBBER_SHA = "a4d9c3b0d54feb7ecc48a56b26b5df9dfaead2125dc575c7949412788c40f75b"

# The baked motion the viewer plays (the model glb carries it) and its clip record.
CLIP_MODEL = "models/shadow/characters/colossus_01_minotaur_a/_cmn/c01_minotaur_a.glb"
CLIP_RECORD = "models/shadow/characters/colossus_01_minotaur_a/_cmn/c01_minotaur_a.clips.json"
CLIP_NAME = "minotaur_a_death_front"
CLIP_FRAMES = 94
CLIP_FPS = 15.08056640625
CLIP_TRACKS = 58
CLIP_SECONDS = (CLIP_FRAMES - 1) / CLIP_FPS  # 6.166877 s: 94 keys span 93 intervals

DEAD_BODY = "models/shadow/characters/colossus_01_minotaur_a_deadbody/_cmn/c01_minotaur_a_dead_body.glb"
DEAD_BODY_MESHES = 6
DEAD_BODY_BYTES = 11925120
DEAD_BODY_PARTS = ("waist", "back", "face", "arms", "chest", "legs")
DEAD_BODY_MATERIALS = {"colossus_01_%s_dead" % part for part in DEAD_BODY_PARTS}
# The corpse's bound layers: the dirt pair the adapter's slot heuristic picks, and the veins veil.
DEAD_BODY_BASE_PNG = "textures/tiling/dirt/_ps4/cmn_dirt_pebbly_01_col.png"
DEAD_BODY_NORMAL_PNG = "textures/tiling/dirt/_ps4/cmn_dirt_pebbly_01_nml.png"
LIVE_BODY = "models/shadow/characters/colossus_01_minotaur_a/_cmn/c01_minotaur_a.glb"
DIRTMOUND = "models/shadow/characters/colossus_01_minotaur_a_deadbody/_cmn/c01_minotaur_a_dead_dirtmound.glb"

# The `death_tentacle.cprt` emitter order -> converted atlas under <out>/textures/.
# (emitter name, library PNG, expected size, expected PNG SHA-256, the cprt's full reference)
# The PNGs are the converter's deterministic output for these containers, so the SHA-256 pins
# the pixels, not just the dimensions (a blank/wrong atlas of the right size fails).
CPRT = "particles/shadow/colossus/death_tentacle/_ps4/death_tentacle.cprt"
CPRT_SIZE = 2376
CPRT_HEADER = (0, 3, 8, 1, 100)
CPRT_EMITTERS = [
    ("main_wisp",
     "textures/vfx/common/smoke/_ps4/8x8_wispysmoke02_1024_white_pfx.png", (1024, 1024),
     "0fd737f277bf3b7077b16f088113acfbd1e2462f01bba8005b84405749dd1a45",
     "$/textures/vfx/common/smoke/_ps4/8x8_wispysmoke02_1024_white_pfx.cptx"),
    ("smoke",
     "textures/vfx/common/smoke/_ps4/8x8_smoke_wispy_sharp_1024_pfx.png", (1024, 1024),
     "135499f5485f2a373fc714f6c2177bfc522549f4604f3aa2f45ac41aaa79c69d",
     "$/textures/vfx/common/smoke/_ps4/8x8_smoke_wispy_sharp_1024_pfx.cptx"),
    ("veins",
     "textures/vfx/common/smokeman/_ps4/2x2_smoke_tendrils_01_pfx.png", (512, 512),
     "2788814f0d2b06d0fbab743685a50a32371ec9919971bbf59d9e061c0df0bd4f",
     "$/textures/vfx/common/smokeman/_ps4/2x2_smoke_tendrils_01_pfx.cptx"),
]

# The ribbon texture is not a .cprt texture: it is the dead-body material's veil layer.
VEINS_PNG = ("textures/shadow/characters/common/_ps4/death_tentacle_veins.png", (1024, 256),
             "f45991263e0ebc329b0bd367c05e561700b0f454c54fcbc718f2f7cc255a3e3c")


class GateError(Exception):
    """A configuration/IO problem, not an asset verdict (exit 2)."""


# --------------------------------------------------------------------------- helpers


def read_bytes(path):
    try:
        with open(path, "rb") as fh:
            return fh.read()
    except OSError as exc:
        raise GateError("cannot read %s: %s" % (path, exc))


def png_fingerprint(path):
    """Validate the PNG and return (width, height, byte size, SHA-256 hex)."""
    data = read_bytes(path)
    if len(data) < 24 or not data.startswith(PNG_MAGIC):
        raise ValueError("not a PNG (signature missing)")
    width, height = struct.unpack(">II", data[16:24])
    if width <= 0 or height <= 0:
        raise ValueError("PNG declares %dx%d" % (width, height))
    return width, height, len(data), hashlib.sha256(data).hexdigest()


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
    textures = doc.get("textures", [])
    images = doc.get("images", [])

    def texture_uri(reference):
        """A texture view's image uri ("" when the view/reference is absent or dangling)."""
        if not isinstance(reference, dict) or "index" not in reference:
            return ""
        try:
            source = textures[reference["index"]].get("source")
            return images[source].get("uri", "")
        except (KeyError, IndexError, TypeError):
            return ""

    def material_detail(material):
        pbr = material.get("pbrMetallicRoughness") or {}
        factor = material.get("emissiveFactor")
        return {
            "base": texture_uri(pbr.get("baseColorTexture")),
            "normal": texture_uri(material.get("normalTexture")),
            "emissive": texture_uri(material.get("emissiveTexture")),
            "emissive_present": "emissiveTexture" in material,
            "emissive_factor": None if factor is None else [float(c) for c in factor],
        }

    animations = {}
    for anim in doc.get("animations", []):
        counts, ends = [], []
        for sampler in anim.get("samplers", []):
            try:
                accessor = accessors[sampler["input"]]
                counts.append(accessor.get("count", 0))
                ends.append((accessor.get("max") or [0])[0])
            except (KeyError, IndexError, TypeError) as exc:
                raise ValueError("animation %r sampler input is not a valid accessor (%s)"
                                 % (anim.get("name", ""), exc))
        animations[anim.get("name", "")] = {
            "keys": max(counts) if counts else 0,
            "seconds": max(ends) if ends else 0.0,
        }
    return {
        "bytes": len(data),
        "meshes": len(doc.get("meshes", [])),
        "materials": [m.get("name", "") for m in doc.get("materials", [])],
        # Material name -> its texture slots and factor. S6 carries the corpse's veins layer
        # through the standard emissive slot; the gate proves the whole binding set.
        "materials_detail": {
            m.get("name", ""): material_detail(m) for m in doc.get("materials", [])
        },
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
        if end < 0:
            raise ValueError("emitter marker at 0x%X is not NUL-terminated" % start)
        markers.append((start, data[start + len(b"death_tentacle:"):end].decode("ascii")))
        cursor = end + 1
    if [name for _, name in markers] != [emitter[0] for emitter in CPRT_EMITTERS]:
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
        if len(data) == expected_size and digest == expected_sha:
            report.good("source", name, "%d B sha256 %s" % (len(data), digest[:16]))
        elif len(data) == KNOWN_CLOBBER_BYTES and digest == KNOWN_CLOBBER_SHA:
            report.warn("source", name,
                        "known 2026-10-22 clobber: a %d B `player_a_stand` JSON dump, not the "
                        "%d B clip; restore it from the pak (docs/notes/s6-recon.md §2.1)"
                        % (len(data), expected_size))
        else:
            report.bad("source", name,
                       "CORRUPTED: %d B, head %s, sha256 %s — expected %d B sha256 %s; "
                       "this is not the shipped clip and is not the known clobber"
                       % (len(data), data[:4].hex(), digest[:16], expected_size, expected_sha[:16]))


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
    except (ValueError, struct.error) as exc:
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
            if summary["bytes"] != DEAD_BODY_BYTES:
                problems.append("%d B, expected %d" % (summary["bytes"], DEAD_BODY_BYTES))
            if summary["meshes"] != DEAD_BODY_MESHES:
                problems.append("%d meshes, expected %d" % (summary["meshes"], DEAD_BODY_MESHES))
            if summary["skins"] != 0:
                problems.append("%d skins, expected 0 (static corpse)" % summary["skins"])
            materials = set(summary["materials"])
            if len(summary["materials"]) != len(DEAD_BODY_MATERIALS):
                problems.append("%d materials, expected %d" % (len(summary["materials"]),
                                                                len(DEAD_BODY_MATERIALS)))
            if materials != DEAD_BODY_MATERIALS:
                missing = sorted(DEAD_BODY_MATERIALS - materials)
                extra = sorted(materials - DEAD_BODY_MATERIALS)
                if missing:
                    problems.append("missing material(s): %s" % ", ".join(missing))
                if extra:
                    problems.append("unexpected material(s): %s" % ", ".join(extra))

            # The whole binding set, not just the veil: base/normal stay the dirt pair the adapter
            # picks today, the veins PNG rides the emissive slot (S6 D9), and the factor stays black
            # (a conformant viewer shows no invented glow). A dangling index fails the resolution.
            def resolved(uri):
                if not uri:
                    return ""
                return posixpath.normpath(posixpath.join(posixpath.dirname(DEAD_BODY), uri))

            for name in sorted(DEAD_BODY_MATERIALS):
                detail = summary["materials_detail"].get(name)
                if detail is None:
                    continue  # the missing/unexpected material problem above already names it
                if resolved(detail["base"]) != DEAD_BODY_BASE_PNG:
                    problems.append("%s: base %r, expected %s"
                                    % (name, detail["base"] or "<none>", DEAD_BODY_BASE_PNG))
                if resolved(detail["normal"]) != DEAD_BODY_NORMAL_PNG:
                    problems.append("%s: normal %r, expected %s"
                                    % (name, detail["normal"] or "<none>", DEAD_BODY_NORMAL_PNG))
                if resolved(detail["emissive"]) != VEINS_PNG[0]:
                    problems.append("%s: emissiveTexture %r, expected %s"
                                    % (name, detail["emissive"] or "<none>", VEINS_PNG[0]))
                if detail["emissive_factor"] != [0.0, 0.0, 0.0]:
                    problems.append("%s: emissiveFactor %r, expected [0, 0, 0]"
                                    % (name, detail["emissive_factor"]))
            if problems:
                raise ValueError("; ".join(problems))
            report.good("corpse", os.path.basename(DEAD_BODY),
                        "%d meshes, 6 materials (%s), 0 skins, %d B, veins emissive on all 6"
                        % (summary["meshes"], ", ".join(sorted(materials)), summary["bytes"]))
        except (ValueError, struct.error) as exc:
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
        except (ValueError, struct.error) as exc:
            report.bad("corpse", os.path.basename(DIRTMOUND), str(exc))

    # The veil is dead-only (design §8.3). The living Colossus glb is already required by the clip
    # check; assert that its materials carry no emissiveTexture, so a re-convert with the wrong
    # scope (veins bound by texture identity) fails here instead of silently darkening the boss.
    live_path = library_path(out, LIVE_BODY)
    if os.path.isfile(live_path):
        try:
            live = glb_summary(live_path)
            veiled = sorted(name for name, detail in live["materials_detail"].items()
                            if detail["emissive_present"])
            if veiled:
                raise ValueError("live material(s) carry an emissiveTexture: %s" % ", ".join(veiled))
            report.good("corpse", os.path.basename(LIVE_BODY),
                        "%d material(s), none carries an emissiveTexture (the veil stays dead-only)"
                        % len(live["materials"]))
        except (ValueError, struct.error) as exc:
            report.bad("corpse", LIVE_BODY, str(exc))


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

    for index, (expected_name, _, _, _, expected_cptx) in enumerate(CPRT_EMITTERS):
        emitter = parsed["emitters"][index]
        problems = []
        if emitter["name"] != expected_name:
            problems.append("named %r, expected %r" % (emitter["name"], expected_name))
        if emitter["cptx"] != expected_cptx:
            problems.append("cptx %r, expected %r" % (emitter["cptx"], expected_cptx))
        for ref in emitter["csdr"] + [emitter["cptx"]]:
            game_rel = ref[2:] if ref.startswith("$/") else ref
            if not os.path.isfile(os.path.join(root, game_rel.replace("/", os.sep))):
                problems.append("game-tree reference missing: %s" % game_rel)
        if problems:
            report.bad("cprt", emitter["name"], "; ".join(problems))
        else:
            report.good("cprt", emitter["name"],
                        "3 csdr (emitter_%d_*) + %s"
                        % (index, os.path.basename(emitter["cptx"])))
    return parsed


def check_textures(out, report, parsed):
    print("textures")
    cptx_by_name = {}
    if parsed is not None:
        cptx_by_name = {emitter["name"]: emitter["cptx"] for emitter in parsed["emitters"]}

    for name, rel, expected_size, expected_sha, expected_cptx in CPRT_EMITTERS:
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
            width, height, size, digest = png_fingerprint(path)
            if (width, height) != expected_size:
                raise ValueError("%dx%d, expected %dx%d" % ((width, height) + expected_size))
            if digest != expected_sha:
                raise ValueError("sha256 %s, expected %s (pixels differ from the converted "
                                 "atlas)" % (digest[:16], expected_sha[:16]))
            report.good("texture", label,
                        "%dx%d, %d B, sha256 %s" % (width, height, size, digest[:16]))
        except (ValueError, struct.error) as exc:
            report.bad("texture", label, str(exc))

    rel, expected_size, expected_sha = VEINS_PNG
    path = library_path(out, rel)
    label = os.path.basename(rel)
    if not os.path.isfile(path):
        report.bad("texture", label, "missing: %s" % path)
    else:
        try:
            width, height, size, digest = png_fingerprint(path)
            if (width, height) != expected_size:
                raise ValueError("%dx%d, expected %dx%d" % ((width, height) + expected_size))
            if digest != expected_sha:
                raise ValueError("sha256 %s, expected %s (pixels differ from the shipped "
                                 "veil texture)" % (digest[:16], expected_sha[:16]))
            report.good("texture", label,
                        "%dx%d, %d B, sha256 %s" % (width, height, size, digest[:16]))
        except (ValueError, struct.error) as exc:
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
    print("checked %d required asset(s): %d ok, %d failed"
          % (total, report.ok, len(report.failures)))
    if report.warnings:
        print("source warnings: %d (not required assets — the known game-tree clobber; see "
              "docs/notes/s6-recon.md §2.1)" % len(report.warnings))
    if not args.check:
        print("inventory only — run with --check to gate")
        return 0
    if report.failures:
        for asset, detail in report.failures:
            print("  missing/invalid: %s — %s" % (asset, detail))
        print("S6 ASSET GATE FAILED — %d required asset(s) missing or invalid"
              % len(report.failures))
        return 4
    if report.warnings:
        print("S6 ASSET GATE PASSED (%d source warning(s))" % len(report.warnings))
    else:
        print("S6 ASSET GATE PASSED")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except GateError as exc:
        print("S6 ASSET GATE ERROR: %s" % exc, file=sys.stderr)
        sys.exit(2)
