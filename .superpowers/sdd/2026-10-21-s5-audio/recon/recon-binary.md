# S5 Audio — binary recon (`eboot.bin` / `.sxd` codec)

Status: **recon complete, nothing modified.** No file in the repo, the game tree, or the existing
Ghidra project (`out/re/ghidra/sotc.gpr`) was written to. The only artifact created is this report;
the throwaway Ghidra project used for the import test lived under `$TMPDIR` and has been deleted.

Date: 2026-10-21. Host: macOS, Apple Silicon.
Related: `recon-integration.md`, `recon-sdl3.md` (same directory).

---

## 0. Headline

| # | Question | Answer | Confidence |
|---|---|---|---|
| 1 | Is the binary analyzable? | **Yes.** `eboot.bin` is a PS4 SELF whose code segments are **plaintext** — not encrypted, no keys needed. | **Verified** (byte-exact) |
| 2 | Is there a decrypted ELF? | **Yes, already in the repo**: `out/re/eboot.elf` (62 MB), byte-identical to the SELF's segment payload, `ET_SCE_EXEC_ASLR`. | **Verified** |
| 3 | Codec behind the `.sxd` files? | **ATRAC9 (AT9)**, decoded by **`libSceNgs2`** (NGS2 AT9 decoders), with a secondary **SCE ADPCM** path for streamed SFX. No CRI/HCA/Vorbis/CELT/Opus/MP3/AAC evidence at all. | **High** for ATRAC9-in-engine, **medium-high** for the `.sxd2` payload specifically |
| 4 | Magic `SHDF`? | **Wrong in the brief.** Every sound bank is **`SXDF`** (`53 58 44 46`), streams are **`SXDS`**. `SHDF` occurs **0** times in the binary and **0** files. | **Verified** (counted) |

**This answers the gating open question in `recon-integration.md:634` ("Codec. ATRAC9 or not?").**
It is ATRAC9, and the reason the bytes look undecodable is that the payload is a *raw ATRAC9
frame stream*, not a RIFF/AT9 container — hence no `AT9` / `RIFF` / `fmt ` 4CC anywhere in the
files.

---

## 1. Container: `eboot.bin`

### 1.1 Identity

```
$ file eboot.bin
eboot.bin: data
$ xxd -l 32 eboot.bin
00000000: 4f15 3d1d 0001 0112 0101 0000 8004 7004  O.=...........p.
00000010: 9491 e403 0000 0000 0a00 2200 0000 0000  ..........".....
```

| Field | Value |
|---|---|
| Path | `SOTC-extract/extracted/uroot/eboot.bin` |
| Size | 65,314,052 B (62.3 MiB) |
| SHA-256 | `21982727c6f97fd415b3af5e99c5e071619c4dcc2248787a701a31a45ab0a4e0` |
| Magic @0x00 | `0x1D3D154F` — PS4 **SELF** (not ELF) |
| Version @0x04 | `0x12010100` |
| ELF filesize @0x10 | `0x03E49194` = 65,314,196 (≈ file size) |

`file` reports "data" because no host tool recognises the SELF wrapper — expected.

### 1.2 SELF header and segment table (parsed by hand)

The ELF image starts at **SELF offset `0x160`**; `eboot.elf[0:0x40] == eboot.bin[0x160:0x1A0]`
(verified). Between `0x20` and `0x160` sits a 5-entry block/segment table, each entry `0x20` bytes
split into two halves. The **second half of each entry is the segment's real position and size in
the SELF**, and every size reproduces a `PT_*` `filesz` exactly — which is what makes the table
legible and lets you convert between the two files without guessing:

| entry | flags/id | SELF offset | size | matches |
|---|---|---|---|---|
| 1 | `0x0004` / `0x11` | `0x00B590` | `0x1593978` | `PT_LOAD[0]` filesz |
| 2 | `0x0004` / `0x31` | `0x159F290` | `0x6D190` | `PT_SCE_COMMENT` filesz |
| 3 | `0x0004` / `0x51` | `0x16182C0` | `0x17D3100` | `PT_LOAD[1]` filesz |
| 4 | `0x0004` / `0x71` | `0x2DF3680` | `0x1055AB0` | `PT_SCE_MODULE_INFO` filesz |
| 5 | `0x0004` / `0x91` | `0x3E49150` | `0x44` | `PT_0x6fffff00` filesz |

Those offsets *are* the conversion factor: `0xB590 - 0x4000 = 0x7590` (LOAD0 delta) and
`0x16182C0 - 0x1608000 = 0x102C0` (LOAD1 delta) — the two numbers measured independently in §1.4.

The `0x0004` flag bit is *nominally* a SELF cipher/hash attribute, but its semantics do not matter
here because the payloads are demonstrably readable (§1.4). Do **not** infer "encrypted" from it.

### 1.3 The ELF inside

`out/re/eboot.elf`:

```
$ file out/re/eboot.elf
ELF 64-bit LSB x86-64, version 1 (FreeBSD)
$ python3 parse:
  e_type=0xfe10  e_machine=0x3e  e_version=1  e_entry=0xf120  e_phoff=0x40
  e_shoff=0  e_flags=0x0  e_ehsize=64  e_phentsize=56  e_phnum=11
```

`e_type = 0xFE10` is **`ET_SCE_EXEC_ASLR`** — a SCE-specific type, not `ET_EXEC`. That is why
`readelf -h` says *"is not an object file"* and why `readelf -lW` prints nothing: it rejects the
type before it ever looks at the program headers. The file itself is perfectly well formed.

Program headers:

| # | type | file off | vaddr | filesz | memsz | flags |
|---|---|---|---|---|---|---|
| 0 | `LOAD` | `0x004000` | `0x0` | `0x1593978` | `0x1593978` | R+X |
| 1 | `SCE_COMMENT` | `0x01598000` | `0x01594000` | `0x6D190` | `0x70000` | R |
| 2 | `LOAD` | `0x01608000` | `0x01604000` | `0x17D3100` | `0x2352AA4` | R+W (BSS tail) |
| 3 | `SCE_REL` | `0x01608000` | `0x01604000` | `0x50` | `0x50` | R |
| 4 | `DYNAMIC` | `0x03E2FD30` | `0x0` | `0xE80` | `0xE80` | R+W |
| 5 | `INTERP` | `0x004000` | `0x0` | `0x15` | `0x15` | R (`"/libexec"`…) |
| 6 | `TLS` | `0x01608060` | … | `0x4` | `0x20` | R |
| 7 | `0x6474E550` (GNU_EH_FRAME-like) | `0x0151008C` | `0x0150C08C` | `0x878EC` | | R |
| 8 | `SCE_MODULE_INFO` | `0x02DDB100` | `0x0` | `0x1055AB0` | 0 | R |
| 9 | `0x6FFFFF00` | `0x03E30BB0` | `0x0` | `0x44` | 0 | — |
| 10 | `0x6FFFFF01` | `0x03E30BF4` | `0x0` | `0xB70` | 0 | — |

**Address mapping used throughout this report** — both LOADs satisfy `vaddr = file_offset - 0x4000`,
so for `eboot.elf`: `vaddr = file_off - 0x4000`, image base 0, entry `0xF120`.
(Sanity: bytes at `0xF120` = `55 48 89 E5 41 57 41 56 53 50` = `push rbp; mov rbp,rsp; push r15…`
— a textbook x86-64 prologue.)

### 1.4 Proof that nothing is encrypted

Two independent checks, both on the real bytes:

**a) Byte-exact identity with the reconstructed ELF.** For every 4 KiB page of both LOAD ranges:

| range | bytes | pages differing | identical |
|---|---|---|---|
| `LOAD[0]` (code+rodata) | 22,624,632 | **0** / 5,523 | **100.00 %** |
| `LOAD[1]` (data) | 24,981,760 | **0** / 6,099 | **100.00 %** |

Constant per-region deltas: `0x7590` for LOAD0, `0x102C0` for LOAD1 (the SELF inserts block
headers/padding between segments; `eboot.elf` is the SELF with those bytes stripped —
`65,314,052 - 65,214,308 = 99,744 = 0x185A0`).

**b) Entropy / code statistics.** Code region (`elf 0x4000..0x104000`):

```
entropy = 6.41 bits/byte   zeros = 13 %   printable = 30.4 %
```

Ciphertext would sit at ≈ 7.99 with 0 % zeros and ≈ 37 % printable. 6.41 + 30 % printable +
plausible x86-64 bytes = **plain ordinary compiled code with dense string tables**.

**c) `SXDF` compare instructions decode as real code** (e.g. at `0x00985A7C`:
`41 81 7D 00 53 58 44 46` = `cmp dword ptr [r13+0], 0x46445853` = `"SXDF"`), which is only
possible on decrypted text.

**Conclusion: no keys are needed, the game's own crypto is not in the picture, and the SELF is
analyzable as-is.** The myth that a PS4 `eboot.bin` needs a decryption key applies to PS3 SELF;
this PS4 title is signed only, not encrypted.

### 1.5 Tool verdicts

| Tool | Target | Result |
|---|---|---|
| `file` | `eboot.bin` | `data` (no SELF support) |
| **REA** `open_binary` | `eboot.bin` | **refused**: `target_unavailable` / `"Cannot open artifact: unsupported binary format"` — REA's formats are `elf/mach-o/pe/dos-*/zip/…`, **no SELF** |
| **REA** `open_binary` | `out/re/eboot.elf` | accepted: `format: elf`, `architecture: x86_64`, sha256 `d0723afb…c25861d3` |
| **REA** `get_navigation_context` | — | `No app is open` (nothing bound; expected before `open_binary`) |
| `readelf -h/-l` | `eboot.elf` | **"is not an object file"** (rejects `e_type 0xFE10`) |
| **Ghidra 12.2 headless** | `eboot.elf` | **import OK**: `Using Loader: Executable and Linking Format (ELF)`, `Language/Compiler: x86:LE:64:default:gcc`, `Import succeeded` |

Ghidra's only complaint is benign and worth recording:

```
Additional info: … ELF Dynamic table appears to have been stripped from binary
```

Ghidra's ELF loader does not understand the SCE dynamic tags (`0x610000xx`), so it drops the
dynamic table. It still creates both `PT_LOAD` blocks and the correct base, so disassembly,
xrefs and the decompiler all work. Anyone re-importing should expect that message.

### 1.6 Existing assets in the tree (do not rebuild)

| Path | Size | Note |
|---|---|---|
| `out/re/eboot.elf` | 65,214,308 B | the decrypted ELF (sha256 `d0723afb…`) — **reuse this** |
| `out/re/ghidra/sotc.gpr` | 0 B | an empty/stub Ghidra project from the earlier S4 RE pass |
| `out/re/ghidra/sotc.rep/` | — | project data dir, no analysed program committed |

No `*.dec`, no `decrypted/` tree, no other `*.elf` anywhere under `Downloads/sotc`
(`find . -name '*.elf' -o -name '*.dec' -o -iname '*decrypt*'` → only `out/re/eboot.elf` and
unrelated C# source files in `tools/`).

### 1.7 `sce_module/` inventory

```
uroot/sce_module/:  libSceFios2.prx (446,089 B)   libc.prx (1,069,730 B)
uroot/sce_sys/:     about/, keystone     uroot/: cachedFiles.psarc, coredata, misc,
                    pak0..pak3.psarc, pakN.psarc, sce_discmap.plt
```

Only **two** Sony modules ship loose, and **neither is audio**:

| module | magic | plaintext? |
|---|---|---|
| `libc.prx` | SELF `4F153D1D`, `elf_filesize 0x105298` | **yes** — `strings` shows x86-64 prologues (`AWAVAUATSPH`, `[A\A]A^A_]`) |
| `libSceFios2.prx` | SELF `4F153D1D`, `elf_filesize 0x6CE78` | **yes** (same) |

There is **no** `libSceAudioOut.prx`, `libSceAtrac*.prx`, `libSceAjm.prx`, `libSceNgs2.prx` or
`libSceAudio3d.prx` on disk — those live in the console firmware, not the game package. So the
audio *system* libraries are not available for local analysis; only the game's use of them is.
`pakN.psarc` is the only other container of interest for audio assets (all `sound/` files are
already extracted outside it).

---

## 2. Audio surface recovered from the binary

### 2.1 Imported SCE libraries (from `SCE_MODULE_INFO` @ file `0x2DDB100`)

readable, plaintext, in declaration order:

```
libSceGnmDriver  libSceFiber   libSceNet      libSceNetCtl   libkernel      libSceSysmodule
libSceVideoOut   libScePad     libSceUserService  libSceIme   libSceAudioIn  libSceAudioOut
libSceNpToolkit2 … libSceAjm   …              libSceNgs2     libc           libScePngEnc
libSceAudio3d    libSceRazorCpu
```

The audio-relevant subset, with addresses:

| library | vaddr of name | why it matters |
|---|---|---|
| `libSceAudioOut.prx` | `0x02DD71D5` | the only PCM output path |
| `libSceAudioIn.prx` | `0x02DD71C3` | mic (voice capture only) |
| **`libSceNgs2.prx`** | `0x02DD7476` | **NGS2 — the mixing/voice engine that owns the ATRAC9 decoder** |
| **`libSceAudio3d.prx`** | `0x02DD749F` | positional audio (Sndx integrates it) |
| **`libSceAjm.prx`** | `0x02DD741E` | Audio Job Manager (job-queue decoder dispatch) |
| `libSceVoice.prx` | `0x02DD7305` | chat voice, not game audio |

Also present: `Shadow_Shipping` (`0x02DD77AC`) and the build path
`d:/dev/cp10/Main/Build/Dev/Bin64/Shadow_Shipping.elf` (`0x02DD7803`) — the project is
**"cp10"**, engine is **BPE** (Bluepoint Engine).

### 2.2 Limitation: the import symbols are hashed

The per-library symbol tables are **not** name-resolved. Immediately after the library names the
string table holds base64-looking **hashes**:

```
0x02DD7838  'xnd8BJzAxmk#t#Z'   0x02DD7848  'R9lA82OraNs#t#Z'
0x02DD7858  'UmCvjSmuZIw#6#w'   0x02DD7868  'jO2tec4dJ2M#6#w'   … 595 candidates
```

So `sceAudioOutOpen`, `sceNgs2SystemCreate`, `sceAjmDecode` etc. **cannot be recovered by name**
from this build; they resolve through SCE's 64-bit symbol hashes. Practical consequence: you can
see *that* NGS2/Audio3d/Ajm are used and where the calls are, but you must identify individual
stubs by call-site behaviour or by hashing known API names, not by strings.
(51 `0x61000015` and 1 `0x61000017` dynamic entries carry lib-id + name-offset pairs pointing into
this table.)

### 2.3 The game's own audio engine: **Sndx**

The game does not call NGS2 directly for content. It uses **Sndx**, a Sony middleware layer
(`sceSndx*` / `Sg2*` API), wrapped by Bluepoint:

```
0x012B671A  '=== sndx system initialize ... ==='
0x012B68C8  'sce_sndx_flush'  'sce_sndx_update'  'sce_sndx_render'
0x012B6A2F  'sceSndx-async-thread'
0x012B6AB6  'audioOutCapture'  0x012B6AC6 'bussId'  0x012B6ACD 'sndx_capture_notify'
0x012B67B0  'isSkipStartAudioOut'  0x012B67E8 ' Started audio out service'
0x012B6799  ' Set output mode (%d)'
0x012B660B  'sceSndxManualRenderStop'  0x012B6623 'sceSndxManualRenderWrite'
0x012B548F  'sceSndxInstanceSetBank'  0x012B54E7 'sceSndxInstanceSetPriorityOffset'
0x012B551D  'sceSndxInstanceSetStartPosition'  0x012B5549 'sceSndxInstanceSetPreDelay'
0x012B558A  'sceSndxInstanceWriteStreamData'  0x012B55C4 'sceSndxInstanceWriteStreamTermination'
0x012B5646  'sceSndxInstanceSetVariableByIdF'  0x012B567F 'sceSndxInstanceSetVariableByIdS'
0x012B56E0  'sceSndxInstanceSetVariableByName…'
0x012CF097  'd:\dev\cp10\Main\Code\BPE\Source\Engine\Sound\SndxSoundTable.cpp'
0x012D30D8  'void SndxSoundTable::LoadSoundTable(const bpe::String &)'
0x012D3111  'Duplicate entry in sound table (%s) for sound (%s)'
0x012C4E79  'd:\dev\cp10\Main\Code\BPE\Source\Engine\Sound\SoundSystemSndx.cpp'
0x012C4EC7  'void SoundSystemSndx::Init(const SndxInitParams &)'
0x012C4EFA  'maxResources=128 cpuEnableFlags=0000001 maxControllerSpeakers=1 maxXfaderShortcuts=300'
0x012B6852  'maxRandomGenerators'  0x012B6884 'fcqMode'
0x012B68A1  'AUTO' 'REPLACE' 'AVERAGE'   ← fcqMode enum values
0x012B6A8E  'cpuEnableFlags' '0000110'
```

So the runtime chain is: **BPE SoundSystemSndx → Sndx → NGS2 (voices/mix) + Audio3d (positioning)
+ AudioOut (PCM out)**, with Ajm available for job-based decode. 314 `sndx`-related strings are
present; the Sndx init params are the `SndxInitParams` struct, printed verbatim at
`0x012C4EFA`.

### 2.4 Sndx debug/console variables (useful for a future smoke test)

```
0x012677B4  'Show ADPCM Streams'     (xref @ 0x005C152D — a dev/console screen)
0x012D087B  '[  Mode   ] Adpcm Stream Module'
0x012B34F5  '$%x,$%p,$%p:SFX_PLAY_WAVEFORM %d,%d,%d,%d'
0x012AC5EA  '$%x,$%p,%d:VOICE_FINISHED'
0x012AC61F  '$%x,$%p,%d:VOICE_ALLOC_AT9 %d/%d'
0x012AC641  '$%x,$%p,%d:VOICE_ALLOC_CHANNEL %d/%d'
```

That log vocabulary (`SFX_PLAY_WAVEFORM`, `VOICE_ALLOC_AT9`) is the exact event stream a decoder
validator would want to correlate against.

---

## 3. The `.sxd` container — corrected and parsed

### 3.1 The magic is `SXDF`, not `SHDF`

| check | result |
|---|---|
| `eboot.elf.count(b'SHDF')` | **0** |
| `eboot.elf.count(b'SXDF')` | **8** (real `cmp`/`mov` immediates, §3.4) |
| files under `game/sound/` with magic `SHDF` | **0** |
| files under `game/sound/` with magic `SXDF` | **30** (`.sxd`, `.sxd1`) |
| files with magic `SXDS` | **5** (all `.sxd2`) |

`0x48` (`H`) vs `0x58` (`X`) is a single-bit transcription difference; the brief's `53 48 44 46`
does not exist on disk. Both sibling reports reached the same conclusion independently
(`recon-integration.md:16`, `recon-sdl3.md:346`).

### 3.2 Header layout (little-endian, verified across 35 files)

```
0x00  char[4]  "SXDF"  (bank/header)   or  "SXDS"  (stream payload)
0x04  u16      version = 4
0x06  u16      (varies per file; hash/checksum — meaning unverified)
0x08  u32      own file size          ← exact match on every bank checked
0x0C  u32      companion .sxd2 size   ← music.sxd1: 9872 B file declares 0x0A461D20 = 172,367,136
                                        = music.sxd2's exact size
0x10  byte[16] GUID / file hash       e.g. boss01 = 3817adac 981f914c a9b41467 94f91bb1
0x20  char[0x40] name, NUL-padded     e.g. "boss01", "sotc_buss", "music"
0x60  chunk chain starts here
```

Verified pairs:

| file | size | field @0x08 | field @0x0C | partner size |
|---|---|---|---|---|
| `boss01.sxd` | 15,967,712 | `0xF3A5E0` ✓ | 0 | — |
| `music.sxd1` | 9,872 | `0x2690` ✓ | `0x0A461D20` = 172,367,136 | `music.sxd2` = 172,367,136 ✓ |
| `sotc_buss.sxd` | 215,504 | `0x349D0` ✓ | 0 | — |

So `.sxd1` is a **tiny index** that points into the huge `.sxd2`; `.sxd` banks are self-contained.
That matches the sibling's chunk-chain finding and means a decoder must handle the pair, not the
file alone.

### 3.3 Chunks (cross-checked against the sibling's chunk list)

Observed directly: `TONE` at `0x60` in **both** `boss01.sxd` and `music.sxd1`; `WAVE` at `0x68C0`
in `boss01.sxd`. Siblings list the full chain
`TONE, REQD, NAME, WAVE, SEQD, LVAR, LVRN, RXFD, RXFN, DATA`.

Per-wave record inside `WAVE` (stride consistent, fields inferred from alignment):

```
01 00 00 00   21 02 ff 00   80 BB 00 00   66 36 00 00   …
^flags        ^format word  ^48000        ^sample/byte count
```

The **`0xBB80` = 48000** field is the strongest structural confirmation: every wave record carries
an explicit 48 kHz rate, so the bank is *uncompressed-rate metadata + encoded payload*, not a
self-describing RIFF.

**Format word** (this is the field `recon-integration.md:637` could not split):

| value | count in `boss01.sxd` | reading |
|---|---|---|
| `0x00FF0221` | 53 | format id `0x21`, **2 channels** |
| `0x00FF0121` | — | format id `0x21`, **1 channel** |
| `0x00FF0220` | 3 | same family, 0 channels (template/legacy) |
| `0x00FF0222` | 3 | same family |

Byte 0 is **`0x21` in every real wave record across every bank sampled**; only the channel-count
byte varies. That is the signature of **one encoder/one codec** used for all wave content.
Confidence: *inferred, medium* — the split is consistent with the observed 1-/2-channel
per-record variation, but is not proven without a decoder round-trip.

### 3.4 Codec determination

**Positive evidence (all in `eboot.elf`):**

| evidence | vaddr | xref call-site |
|---|---|---|
| `maxAtrac9Decoders` (NGS2 system-init option) | `0x012AC36E` | `0x0094876A` |
| `VOICE_ALLOC_AT9 %d/%d` (voice allocator) | `0x012AC61F` | `0x0094A7B5` |
| `Invalid ATRAC9 configure data (%08X)` | `0x012B5660` | `0x0098429E` |
| `Unsupported waveform type (%d)` | `0x012AC667` | `0x0094A700`, `0x0094B9A0` |
| `Unknown waveform data (%d)` | `0x012B55B0` | `0x00980BB5`, `0x00983284`, `0x0098330F` |
| `Unsupported wave format (%d)` | `0x012B5686` | `0x0098318E`, `0x00984004` |
| `Invalid waveform data (%d,%dch,%ds)` | `0x012B55CC` | — |
| `Wave stream needed streaming buffer (%X)` | `0x012B56A4` | — |
| `Unattached stream file (%d)` / `Couldn't allocate stream (%d)` | `0x012B5624` / `0x012B5641` | — |

The **ATRAC9 config validation sits inside the same routine as the generic wave-format
validation** (`0x0098429E` sits `0x29A` bytes after the `0x00984004` "Unsupported wave format"
assert — same function range, same error-reporting helper), and inside it the code is a
**bit-field unpacker** using BMI2 `shlx`:

```
0x00984230: … 89 fa 45 8b 04 96 44 89 c2 81 e2 00 00 00 ff 81 fa 00 00 00 fe 75 47
              0f b6 54 c8 05 ba 05 0b 00 00 ff c7 c4 c2 68 f7 d0 ff c2 44 0f af d2
              c4 c2 20 f7 d0 42 0f b6 14 22 …
```

`81 FA 00 00 00 FE` = `cmp edx, 0xFE000000`, `C4 C2 68 F7 D0` = `shlx`, plus `movzx …+5` byte
extraction — i.e. **a packed config word is being decomposed**, exactly what an ATRAC9 config
(the AT9 `config_data` structure: sample rate, channel config, frame size, superframe index)
requires. This is the strongest single piece of evidence that the wave data is ATRAC9.

**Corroborating measurements:**

- `music.sxd2` payload: entropy **7.971 bits/byte**, 0 % zero bytes, zlib ratio **0.978** —
  incompressible perceptual-codec bitstream, *not* PCM and *not* raw ADPCM.
- ATRAC9 at 48 kHz stereo lives naturally in this range; the game's own allocator logs
  `VOICE_ALLOC_AT9` with a decoder budget.

**Negative evidence (searched the whole 62 MB, case-insensitive, on extracted strings):**

| codec | string hits |
|---|---|
| `vorbis`, `celt`, `opus`, `flac` | **0** |
| `mp3` (as a real string) | **0** |
| `HCA`, `cri` (as real strings — CRIWARE) | **0** (only incidental byte matches inside code) |
| `at3` | 0 real (only `…At30Hz`, `Mat33`, name substrings) |
| `AT9` 4CC / `RIFF` / `fmt ` / `data` inside any `.sxd` | **0** |

So CRI (ADX/HCA), Vorbis, CELT/Opus and MP3 are **not** in this title's audio path.

**Secondary codec: SCE ADPCM.** A parallel ADPCM path exists and is used for streamed SFX:

```
0x01273653  'B_adpcmOpenReq'
0x01273662  'Sg2ExStAdpcmOpen error %d'          xref @ 0x006B162A
0x01273688  'ADPCM Manager:'
0x0127379A  'd:\dev\cp10\Main\Code\Game\CP10\Shadow\sources\manager\sound\src\adpcmManager.cpp'
0x012737EC  'void adpcmFileInit()'               xref @ 0x006B1F2A
0x0127380A  ':segHeader not resolve'
0x01026169  'adpcm-condition-def'  'adpcm-func-def'  'adpcm-orient'  'adpcm-package-def'
0x0102AEB9  'adpcm-volume-control'  'adpcm-scr-equ'
0x012677B4  'Show ADPCM Streams'
```

and a RIFF tag table that accepts both `VAGp` and `SXDF` forms:

```
0x012BA745  'RIFF' 'FORM' 'VAGp' 'SXDF' 'fmt ' 'smpl' 'fact' 'data' 'cue ' 'adtl' 'INFO'
            xrefs @ 0x009B3F25, 0x009B5A89
```

`VAGp` = PS-ADPCM ("VAG") RIFF form. So Sndx can ingest VAG/ADPCM waves as well — but the shipped
`.sxd` banks contain no RIFF wrapper, so this is a *capability*, not the bank format.

**Verdict:**

| claim | confidence |
|---|---|
| The engine decodes **ATRAC9** via NGS2, with an AT9 decoder budget | **high** — named init option + allocator log + config validator |
| The `.sxd2` / `DATA` payload is **raw ATRAC9 frames** (no container wrapper) | **medium-high** — incompressible 7.97-bit payload, single 0x21 format id, ATRAC9-only decoder path, no RIFF/AT9 4CC present |
| Sndx additionally supports SCE **ADPCM/VAG** waves | **high** (code present), applicability to shipped banks **low** |
| No CRI/HCA, Vorbis, CELT/Opus, MP3, AAC | **high** (exhaustive string search) |

---

## 4. Address table for follow-up work

Everything below is `vaddr` in `out/re/eboot.elf` (`vaddr = file_off - 0x4000`).

### 4.1 `.sxd` / bank loading

| vaddr | what | evidence |
|---|---|---|
| `0x01273840` | format string `$/sound/shadow/%s.sxd` | xref @ `0x006B3209` |
| `0x01273700` | `$/sound/shadow/music.sxd1` | xref @ `0x006B1D84` |
| `0x0127371A` | `$/sound/shadow/str_amb.sxd1` | xref @ `0x006B1DB6` |
| `0x01273736` | `$/sound/shadow/str_bgm.sxd1` | xref @ `0x006B1DF5` |
| `0x01273752` | `$/sound/shadow/str_boss.sxd1` | xref @ `0x006B1E34` |
| `0x0127376F` | `$/sound/shadow/str_event.sxd1` | xref @ `0x006B1E73` |
| `0x01273856` | `$/sound/shadow/****/shadow_sounds.cxml` | xref @ `0x006B3B94` |
| `0x0101E7AB` | `_$/sound/shadow/sotc_buss.sxd` | no direct `lea` (see note) |
| `0x012737EC` | `void adpcmFileInit()` (assert context) | xref @ `0x006B1F2A` |

The whole `.sxd` naming/pairing logic therefore lives in **one function cluster around
`0x006B1xxx–0x006B3xxx`** (`adpcmManager.cpp`, `manager/sound/src/…`). That is the natural first
target for a "how does the game open a bank" decompile.

> Note: `sotc_buss.sxd` shows 0 `lea` xrefs because its reference is built by string
> concatenation at runtime (the `_` prefix at `0x0101E7AB` suggests a category-prefix helper).
> Treat the 0-xref entries in this table as "referenced indirectly", not "dead".

### 4.2 SXDF header validation (`cmp` immediates)

| vaddr | instruction | meaning |
|---|---|---|
| `0x00985992` | `81 BD 70 FF FF FF 53 58 44 46` / `0F 85 …` | `cmp [rbp-0x90], "SXDF"; jne fail` |
| `0x00985A7C` | `41 81 7D 00 53 58 44 46` / `74 27` | `cmp [r13+0], "SXDF"; je ok` |
| `0x0098A353` | `81 3B 53 58 44 46` / `74 23` | `cmp [rbx], "SXDF"; je ok` |
| `0x0098D28D` | `41 81 39 53 58 44 46` / `74 1E` | `cmp [r9], "SXDF"; je ok` |
| `0x009B3BE9` | `41 C7 04 24 53 58 44 46` | `mov dword [r12], "SXDF"` (**writer**/encoder path) |
| `0x009B3D3C` | `81 39 53 58 44 46` / `0F 85` | `cmp [rcx], "SXDF"; jne` |
| `0x009B5AFA` | `81 38 53 58 44 46` / `75 A7` | `cmp [rax], "SXDF"; jne` |
| `0x012B6B13` | data: `'SNDH'` at `0x12B6B13`, `'SNDF'` at `0x12B6B17` | chunk tags, 4 xrefs |

Four independent validators at `0x98xxxx` confirm the bank loader is real, reachable code and
that the on-disk magic is exactly what the binary compares against.

### 4.3 Chunk-tag helper and `SNDH`/`SNDF`

At `0x00993740` a clean function begins (`90 90 90 90 90 / 55 48 89 E5`) and immediately does:

```
mov rdi, [rip+…]        ; a global tag pointer
lea rsi, [rip+0x9233C1] ; = 0x012B6B13  ("SNDHSNDF")
mov edx, 4
call <helper>           ; 4-byte compare
```

i.e. a **`tag == "SNDH"` predicate** built on one 8-byte literal that also serves `"SNDF"` at
`+4`. Call sites: `0x0099374B`, `0x009938A6`, `0x00993A1D`, `0x00997B20`. This is the cheapest
hook for mapping the header/data chunk split (`SNDH` = header, `SNDF` = data) even though the
shipped banks use the `TONE/WAVE/…` chain instead.

### 4.4 NGS2 / voice layer

| vaddr | what | xref |
|---|---|---|
| `0x012AC2D1` | `Failed to load Audio3d module (%X)` | — |
| `0x012AC30D` | `Failed to load NGS2 module (%X)` | — |
| `0x012AC32E` | ` Loaded NGS2 module` | — |
| `0x012AC343` | `numGrains` (NGS2 init) | — |
| `0x012AC357` | `numDecoderMemoryBlocks` | — |
| `0x012AC36E` | `maxAtrac9Decoders` | `0x0094876A` |
| `0x012AC380` | `maxVoiceChannels`, `maxSubmixers` | — |
| `0x012AC5EA` | `VOICE_FINISHED` | — |
| `0x012AC61F` | `VOICE_ALLOC_AT9` | `0x0094A7B5` |
| `0x012AC641` | `VOICE_ALLOC_CHANNEL` | — |
| `0x012AC69F` | `Failed to initialize Audio3d` | — |
| `0x012ACB03` | `SCCOMP` / `CHMIX` (submixer node types) | — |

`numGrains`, `maxVoiceChannels`, `maxSubmixers`, `numDecoderMemoryBlocks`, `enableACP` are
literally the **`SceNgs2SystemOption` fields**, so `0x0094876A` is inside the NGS2 system-create
call — decompiling it gives the exact voice/decoder budget the game requests (useful sanity bound
for any offline decoder: how many AT9 streams are simultaneously live).

### 4.5 Assert discipline (why decompiling is cheap here)

Audio code uses assert macros that embed file + line:

```
0x00983140: lea rcx, [rip+…]   ; "…adpcmManager.cpp" or similar
            mov edi, -11       ; PE_ASSERT_* id
            mov edx, 0x793     ; line 1939
```

Every audited string site sits next to its `file:line`, so function attribution does not require
full-binary analysis — decompile a single function and you know exactly which `BPE/…/Sound/*.cpp`
it came from.

---

## 5. What was and was not done (honest scope)

**Done**

- SELF header + 5-entry block table parsed and cross-checked against `PT_*` sizes.
- Encryption refuted byte-exactly (100 % page match on both LOADs) and statistically.
- All three tool verdicts recorded (REA refuses SELF, accepts ELF; `readelf` refuses `e_type
  0xFE10`; Ghidra imports cleanly as `x86:LE:64:gcc`).
- Full string census of the 62 MB ELF (222,634 strings) filtered for every audio/codec keyword in
  the brief plus a dozen extras.
- Verified RIP-relative xref scan of LOAD0 (253,961 candidates; validated against known
  instruction windows, e.g. `0x0098D75C → 'priority'`).
- `.sxd` / `.sxd1` / `.sxd2` headers parsed across 35 files; header field semantics confirmed by
  size cross-check.
- Codec determined with explicit confidence per claim.

**Not done (deliberately, time-boxed)**

- **No full Ghidra auto-analysis.** Import was done with `-noanalysis`; a 62 MB x86-64 binary with
  ~10⁵ functions takes tens of minutes and was not needed to answer the question. Function
  boundaries quoted above are *not* Ghidra-confirmed — they are inferred from prologues/padding
  and are labelled as such (only `0x00993740` is a confident function start, bounded by `0x90`
  padding and a `55 48 89 E5` prologue).
- **No decompilation.** Addresses, instruction bytes and string xrefs only.
- **No codec round-trip.** Whether the `.sxd2` bytes decode as ATRAC9 is *inferred*, not proved by
  decoding. That proof needs an ATRAC9 decoder (see §6).
- **No `.prx` analysis.** Only two non-audio modules ship loose; there is nothing audio-side to
  analyse locally.
- **`cxml`/`calt` vocabulary** is the sibling's territory (`recon-integration.md` §0 item 3) and
  was not re-derived here.

**Caveats**

- `eboot.elf` is a *reconstruction* (SELF minus `0x185A0` of block headers/padding). Its declared
  vaddrs match the SELF bytes exactly (100 %), so xrefs are trustworthy; but the original SELF's
  segment `file_offset` values are **not** the ELF's (deltas `0x7590` / `0x102C0`). Never mix a
  "SELF offset" with an "ELF offset" without converting.
- Field `@0x06` of the SXDF header and the `0x00FF` half of the format word remain unidentified.
- The 16-byte GUID at `0x10` was tested as a possible XOR key against `SNDH/SNDF/TONE/SXDF/RIFF`
  at five offsets — **no hit**, so the banks are not trivially XOR-obfuscated with it. Its purpose
  (content hash vs. key) is open.
- All Ghidra work used a throwaway project at
  `$TMPDIR/opencode/s5audio/sotcproj`; the repo's `out/re/ghidra/sotc.gpr` was **not** touched.

---

## 6. Consequence for the S5 milestone

1. **The gate in `recon-integration.md:634` is answered: ATRAC9.** No more guessing; the design can
   commit to an AT9 decode path (or to the D9 fallback if no AT9 decoder is obtainable).
2. **Do not expect a container wrapper.** The payload is raw frames — a RIFF/`AT9` parser will
   find nothing. Frame layout must be derived from the `WAVE` record (48 kHz + sample count +
   byte count) and the AT9 `config_data` the engine validates at `0x0098429E`.
3. **Cheapest next probe** (unchanged in spirit from the sibling's suggestion, now with a target):
   take one short `WAVE` record from `boss01.sxd` (e.g. the `mm_…` entries near `0x68C0`), try
   ffmpeg / vgmstream / a minimal AT9 decoder with the 48 kHz + 2-channel config, and check for
   non-silence + a plausible spectrum. `voice`/`sfx` banks are the best first targets because
   their records are small and their expected spectra are simple.
4. **Voice-budget sanity:** decompile `0x0094876A` to read the real `SceNgs2SystemOption` values
   (`maxAtrac9Decoders`, `maxVoiceChannels`, `maxSubmixers`). That bounds how many simultaneous
   AT9 streams the original renderer supports and is a cheap correctness check for a re-implemented
   mixer.
5. **Do not chase CRI.** There is no CRIWARE/ADX/HCA in this title; any plan that assumes it is
   wasted effort.

---

## Appendix A — commands actually run

```bash
# container
file  $SOTC_EXTRACT/extracted/uroot/eboot.bin
xxd -l 256 $SOTC_EXTRACT/extracted/uroot/eboot.bin
readelf -h out/re/eboot.elf          # -> "is not an object file" (e_type 0xFE10)
python3 - <<'PY'                     # SELF block table + ELF phdr parse
struct.unpack_from('<HHIQQQIHHH', e, 0x10)   # e_type=0xfe10 e_machine=0x3e e_phnum=11 …
PY

# plaintext proof (both LOADs, 4 KiB pages)
python3 - <<'PY'
a = elf[0x4000:0x4000+0x1593978];  b = self[0xb590:0xb590+0x1593978]   # 0/5523 pages differ
a = elf[0x1608000:…+0x17D3100];    b = self[0x16182c0:…]                # 0/6099 pages differ
PY

# string census (222,634 strings)
python3 -c "re.finditer(rb'[ -~]{4,}', elf)"  # filtered for SHDF/SXDF/atrac/AT9/Audio/Ngs/Ajm/…

# xrefs (RIP-relative: REX + 8D/8B/89 + modrm 05/0D/15/1D/25/2D/35/3D, target = next_insn + disp32)
python3 - <<'PY'  …  # 253,961 candidates, exact-target filtered
PY

# Ghidra (throwaway project, no analysis, repo untouched)
~/ghidra_12.2_DEV/support/analyzeHeadless "$TMP/opencode/s5audio" sotcproj \
    -import "$TMP/opencode/s5audio/eboot.elf" -noanalysis
# -> Loader: ELF / x86:LE:64:default:gcc / "ELF Dynamic table appears to have been stripped"

# containers
xxd -l 64 game/sound/shadow/boss01.sxd        # 53 58 44 46 = SXDF
python3 - <<'PY'                              # header fields, chunk walk, entropy, zlib ratio
PY

# module inventory
ls -la extracted/uroot/sce_module/            # libSceFios2.prx, libc.prx only
strings -n 8 extracted/uroot/sce_module/libSceFios2.prx | head
shasum -a 256 …uroot/eboot.bin …out/re/eboot.elf
```

## Appendix B — hashes

```
21982727c6f97fd415b3af5e99c5e071619c4dcc2248787a701a31a45ab0a4e0  uroot/eboot.bin
d0723afbdbd4cb3775c683a2b7f09534d371ce6d0b18146fc35b4d50c25861d3  out/re/eboot.elf
1e952f9486c966e8e3058c01902c8d9c7382acfd3030362db19dfd74681f925c  uroot/sce_module/libc.prx
9ff42b3044c78a343223b9ca7ec715f47c0d29c82d6222568451554341d6fd09  uroot/sce_module/libSceFios2.prx
```
