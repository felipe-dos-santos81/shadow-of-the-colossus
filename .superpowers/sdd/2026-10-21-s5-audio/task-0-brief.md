### Task 0: Decode gate — prove SXDF/AT9 before anything else

**Files:**
- Create: `tools/re/audio/s5_decode_gate.py`
- Create: `docs/notes/s5-audio-recon.md`
- Artifact: `out/s5/gate/*.wav` (gitignored)

**Interfaces:**
- Produces: a written **per-bank `WAVE` layout** and the **AT9 config** (format word, channel count) that Tasks 2/3 consume; a gate command that exits non-zero on silence/wrong duration.
- Consumes: the recon facts in the spec §6 and `.superpowers/sdd/s5-audio/recon-assets.md`.

- [ ] **Step 1: Write the SXDF carve script**

`tools/re/audio/s5_decode_gate.py`:
- Read the 0x60-byte header (`SXDF`/`SXDS`, version, size, hash, name).
- Walk chunks from `0x60` (`char[4] magic` + `u32 size`).
- Locate `WAVE`; for `music.sxd1` read record 0 (76 B: `+0x04` fmt, `+0x08` rate, `+0x14` frames, `+0x20` bytes, `+0x24` start) and carve from `music.sxd2` at its `SXDS` payload (`0x60 + start`, length `bytes`).
- For `boss01.sxd` carve one record per distinct format word (44/64-byte records; the builder check measured `0x00FF0121`, `0x00FF0142`, `0x00FF0221`, `0x00FF0242`, `0x00FF0642`); write the record's declared length/start and the DATA base.
- Write raw `.at9` and a minimal RIFF wrapper carrying the fmt word where the decoder needs it.

- [ ] **Step 2: Decode to WAV**

Try in order:
1. FFmpeg's `atrac9` decoder via the RIFF wrapper (build-time subprocess).
2. VGAudio's `At9`/`Atrac9` path in a tiny C# harness (MIT).
3. If both reject the config, read the bit split at the `Invalid ATRAC9 configure data` xref (`0x12B5660`, xref `0x98429E`) in `out/re/eboot.elf` and retry.

Expected: a 48 kHz WAV with the declared duration (Prologue = 10,898,874 samples ≈ 227.06 s; a boss01 record = its `frames/48000`).

- [ ] **Step 3: Verify non-silence and record the layout**

Run: `python3 tools/re/audio/s5_decode_gate.py --root "$SOTC_GAME_ROOT" --out out/s5/gate --check`
Expected: exit 0; every carved WAV has RMS > −60 dBFS, peak ≤ 1.0, duration within 50 ms of `frames/rate`, no NaN.

Write `docs/notes/s5-audio-recon.md` with: the header/chunk layout, the per-bank `WAVE` record layout (stride, field slots, partition check), the AT9 config word(s), the decoded channel count, and the exact decode command.

- [ ] **Step 4: Commit**

```bash
git add tools/re/audio/s5_decode_gate.py docs/notes/s5-audio-recon.md
git commit -m "chore(re): prove the SXDF/AT9 decode"
```

**Hard stop:** if Step 2 cannot produce non-silent PCM after the Ghidra fallback, report BLOCKED with the failing config/command and stop the milestone — no placeholder, no downstream task.

---

