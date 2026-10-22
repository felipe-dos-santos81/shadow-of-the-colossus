### Task 1: Attribution gate — map the curated names to waves

**Files:**
- Create: `tools/re/audio/s5_audition.py`
- Create: `tools/re/audio/s5_map.json` (machine-readable: event, bank, name, wave index, method, confidence)
- Create: `docs/notes/s5-audio-attribution.md`
- Artifact: `out/s5/audition/*.wav` (gitignored)

**Interfaces:**
- Consumes: Task 0's layout/config and the decoded WAV path.
- Produces: the curated map `event -> {bank, name|index, confidence, method}` that Task 3's `--events` list and Task 5's map use.

- [ ] **Step 1: Table path**

Dump `REQD`/`TONE`/`SEQD`/`WAVE` fields for `boss01`, `com_obj`, `com_v`; test candidate sound→wave links (tone id, record index, `SEQD` sequence, `WAVE` tag). If ambiguous, use Ghidra on the `sndx_resource.c` resolver in `out/re/eboot.elf`.

- [ ] **Step 2: Ear path if the table resists**

`s5_audition.py` decodes every `boss01` `WAVE` (138) plus the curated `com_obj`/`com_v` waves to `out/s5/audition/<bank>/w<index>_<name-if-known>.wav`. Listen (or inspect spectrograms) for: ground hit, whoosh, roar/attack, hurt roar, death/rise; sword swish, sword hit, hurt, death, fall.

- [ ] **Step 3: Write the map**

`tools/re/audio/s5_map.json` (the machine-readable authority):
```json
{"version":1,"cues":[
  {"event":"valus.telegraph","bank":"boss01","name":"boss01_vo_atk1","wave":12,"method":"table","confidence":"high"},
  {"event":"player.swing","bank":"com_obj","name":"wnd_LifeSwd_swish_t1_01","wave":3,"method":"audition","confidence":"medium"}
]}
```
`docs/notes/s5-audio-attribution.md` explains one row per event (event, bank, name, `WAVE` index,
method, confidence). The core cues from design goal 3 must all resolve: `valus.telegraph`,
`valus.impact` (club/stomp), `valus.hurt`, `player.swing`, `player.hit`, `player.hurt`,
`player.death`. Ambience/grab/fall/breath may be `deferred` with the reason.

- [ ] **Step 4: Verify every mapped wave decodes**

Run: `python3 tools/re/audio/s5_audition.py --map tools/re/audio/s5_map.json --check`
Expected: exit 0; every mapped index exists, decodes, and its RMS is non-zero.

- [ ] **Step 5: Commit**

```bash
git add tools/re/audio/s5_audition.py tools/re/audio/s5_map.json docs/notes/s5-audio-attribution.md
git commit -m "chore(re): attribute the S5 sound cues"
```

**Hard stop:** if a core cue cannot be mapped (no table link and no confident audition), report BLOCKED with the unresolved names; do not guess.

---

