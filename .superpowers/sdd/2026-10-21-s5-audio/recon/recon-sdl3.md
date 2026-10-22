# S5 recon — SDL3 audio playback, decode/transcode options, offline verification

> Read-only recon for milestone **S5 (audio)**. No code in this repo was changed.
> Written 2026-10-21. Everything below is either cited from docs/source, or marked **[measured]**
> (a probe run on this machine) or **[local]** (a read-only look at the extracted game tree).

**Environment used for the measured numbers**

| Item | Value |
|---|---|
| SDL3 (Homebrew) | **3.4.18** (`sdl3-config --version`) |
| Machine | macOS, arm64 |
| Drivers compiled in this build | `coreaudio`, `disk`, `dummy`, `pulseaudio` (strings probe of `libSDL3.0.dylib`) |
| FFmpeg reference | upstream `master` sources (release tags currently run to **n9.0**/9.0.x) |

---

## 1. SDL3 playback path

### 1.1 The two shapes you can build

SDL3 replaced SDL2's callback-first model with **audio streams bound to devices**. There are exactly
two supported shapes, and choosing the right one is the whole design decision:

| Shape | API | Use when |
|---|---|---|
| **Simplified** — one stream = one device | `SDL_OpenAudioDeviceStream()` | a single source of PCM (music track, one ambient bed). Device opens **paused**; destroying the stream closes the device. |
| **Bound streams** — N streams mixed into one logical device | `SDL_OpenAudioDevice()` + `SDL_CreateAudioStream()` + `SDL_BindAudioStreams()` | **a game** — many simultaneous voices. SDL mixes all bound streams for you. |

The wiki states the mixing contract explicitly:

> "For a playback device, data for all bound streams will be mixed together and fed to the device."
> — <https://wiki.libsdl.org/SDL3/SDL_BindAudioStreams>

That is the recommendation for S5: **one logical device opened once, one `SDL_AudioStream` per
voice, bound in one atomic `SDL_BindAudioStreams` call.** You get per-voice gain, per-voice format
conversion, and SDL's mixer, without writing a mixer thread.

### 1.2 Exact signatures (SDL 3.2.0+, unchanged in 3.4.18)

```c
/* --- lifecycle ------------------------------------------------------------ */
bool             SDL_InitSubSystem(SDL_InitFlags flags);      /* SDL_INIT_AUDIO */
void             SDL_QuitSubSystem(SDL_InitFlags flags);
void             SDL_Quit(void);

/* --- devices -------------------------------------------------------------- */
#define SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK   ((SDL_AudioDeviceID) 0xFFFFFFFFu)
typedef Uint32   SDL_AudioDeviceID;   /* 0 == invalid */

SDL_AudioDeviceID      SDL_OpenAudioDevice(SDL_AudioDeviceID devid,
                                           const SDL_AudioSpec *spec);
SDL_AudioStream       *SDL_OpenAudioDeviceStream(SDL_AudioDeviceID devid,
                                                 const SDL_AudioSpec *spec,
                                                 SDL_AudioStreamCallback callback,
                                                 void *userdata);
void                   SDL_CloseAudioDevice(SDL_AudioDeviceID devid);

bool  SDL_GetAudioDeviceFormat(SDL_AudioDeviceID devid, SDL_AudioSpec *spec, int *sample_frames);
SDL_AudioDeviceID *SDL_GetAudioPlaybackDevices(int *count);   /* SDL_free() the array */
const char *SDL_GetAudioDeviceName(SDL_AudioDeviceID devid);
const char *SDL_GetCurrentAudioDriver(void);
int   SDL_GetNumAudioDrivers(void);
const char *SDL_GetAudioDriver(int index);

/* --- streams -------------------------------------------------------------- */
typedef struct { SDL_AudioFormat format; int channels; int freq; } SDL_AudioSpec;

SDL_AudioStream *SDL_CreateAudioStream(const SDL_AudioSpec *src_spec,
                                       const SDL_AudioSpec *dst_spec);
void             SDL_DestroyAudioStream(SDL_AudioStream *stream);
bool             SDL_SetAudioStreamFormat(SDL_AudioStream *stream,
                                          const SDL_AudioSpec *src_spec,
                                          const SDL_AudioSpec *dst_spec);
bool             SDL_BindAudioStreams(SDL_AudioDeviceID devid,
                                      SDL_AudioStream * const *streams,
                                      int num_streams);
bool             SDL_BindAudioStream(SDL_AudioDeviceID devid, SDL_AudioStream *stream);
bool             SDL_UnbindAudioStream(SDL_AudioStream *stream);
SDL_AudioDeviceID SDL_GetAudioStreamDevice(SDL_AudioStream *stream);

bool  SDL_PutAudioStreamData(SDL_AudioStream *stream, const void *buf, int len);
int   SDL_GetAudioStreamData(SDL_AudioStream *stream, void *buf, int len);
int   SDL_GetAudioStreamAvailable(SDL_AudioStream *stream);  /* bytes readable, -1 on error */
int   SDL_GetAudioStreamQueued(SDL_AudioStream *stream);     /* bytes queued as input */
bool  SDL_FlushAudioStream(SDL_AudioStream *stream);         /* "no more input for now" */
bool  SDL_ClearAudioStream(SDL_AudioStream *stream);         /* drop everything queued */

/* --- gain / transport ----------------------------------------------------- */
bool  SDL_SetAudioStreamGain(SDL_AudioStream *stream, float gain);   /* 1.0 = unity */
float SDL_GetAudioStreamGain(SDL_AudioStream *stream);
bool  SDL_SetAudioDeviceGain(SDL_AudioDeviceID devid, float gain);   /* logical devices only */
bool  SDL_SetAudioStreamFrequencyRatio(SDL_AudioStream *stream, float ratio);

bool  SDL_PauseAudioDevice(SDL_AudioDeviceID devid);
bool  SDL_ResumeAudioDevice(SDL_AudioDeviceID devid);
bool  SDL_AudioDevicePaused(SDL_AudioDeviceID devid);
bool  SDL_PauseAudioStreamDevice(SDL_AudioStream *stream);
bool  SDL_ResumeAudioStreamDevice(SDL_AudioStream *stream);
bool  SDL_AudioStreamDevicePaused(SDL_AudioStream *stream);

/* --- offline helpers ------------------------------------------------------ */
typedef void (SDLCALL *SDL_AudioStreamCallback)(void *userdata, SDL_AudioStream *stream,
                                                int additional_amount, int total_amount);
bool  SDL_SetAudioStreamGetCallback(SDL_AudioStream *stream,
                                    SDL_AudioStreamCallback callback, void *userdata);
bool  SDL_LockAudioStream(SDL_AudioStream *stream);

bool  SDL_LoadWAV(const char *path, SDL_AudioSpec *spec, Uint8 **audio_buf, Uint32 *audio_len);
bool  SDL_ConvertAudioSamples(const SDL_AudioSpec *src_spec, const Uint8 *src_data, int src_len,
                              const SDL_AudioSpec *dst_spec, Uint8 **dst_data, int *dst_len);
bool  SDL_MixAudio(Uint8 *dst, const Uint8 *src, SDL_AudioFormat format, Uint32 len, float volume);
```

Sources: <https://wiki.libsdl.org/SDL3/SDL_OpenAudioDeviceStream>,
<https://wiki.libsdl.org/SDL3/SDL_CreateAudioStream>,
<https://wiki.libsdl.org/SDL3/SDL_BindAudioStreams>,
<https://wiki.libsdl.org/SDL3/SDL_GetAudioDeviceFormat>,
<https://wiki.libsdl.org/SDL3/SDL_GetAudioPlaybackDevices>,
<https://wiki.libsdl.org/SDL3/SDL_SetAudioStreamGain>,
<https://wiki.libsdl.org/SDL3/SDL_SetAudioDeviceGain>,
<https://wiki.libsdl.org/SDL3/SDL_PutAudioStreamData>,
<https://wiki.libsdl.org/SDL3/SDL_GetAudioStreamQueued>,
<https://wiki.libsdl.org/SDL3/SDL_LoadWAV>,
and the header itself: `include/SDL3/SDL_audio.h`.

### 1.3 Recommended flow (the one to implement)

```
startup
  SDL_SetHint(SDL_HINT_AUDIO_DRIVER, ...)        // only for tests/forced backends — BEFORE SDL_Init
  SDL_Init(SDL_INIT_AUDIO)                       // SDL3 does NOT init audio implicitly
  devid = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL)
                                                 // NULL spec = "give me the system's format"
  SDL_GetAudioDeviceFormat(devid, &dev_fmt, NULL) // log it; this is what SDL picked

per voice
  src = { SDL_AUDIO_F32, clip.channels, clip.rate }   // whatever the decoder produced
  stream = SDL_CreateAudioStream(&src, NULL)          // NULL dst => convert to the device format
  SDL_SetAudioStreamGain(stream, voice_gain)          // per-voice volume
  SDL_BindAudioStream(devid, stream)

  SDL_ResumeAudioDevice(devid)                   // logical devices start UNpaused; this is a no-op
                                                 // for them, but harmless and required in the
                                                 // OpenAudioDeviceStream shape

per frame
  for each active voice:
      if (SDL_GetAudioStreamQueued(stream) < low_watermark)      // e.g. 100 ms worth
          SDL_PutAudioStreamData(stream, pcm + cursor, n);       // top up, don't dump the clip
      if (clip finished && !looping) { SDL_ClearAudioStream(stream); deactivate(voice); }

shutdown
  for each voice: SDL_UnbindAudioStream(stream); SDL_DestroyAudioStream(stream);
  SDL_CloseAudioDevice(devid);
  SDL_QuitSubSystem(SDL_INIT_AUDIO);
  SDL_Quit();
```

Why `dst_spec = NULL`: SDL then targets the device's own format, so you never have to match
44.1 kHz/48 kHz by hand, and a device switch keeps working.

### 1.4 Source format → device format conversion

- `SDL_AudioSpec` in SDL3 is **only** `{format, channels, freq}` — `samples`, `silence`, `padding`
  and `callback` are gone; buffer size is `SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES`.
- **Conversion happens on read, not on write.** This is the one behavioural change that trips people
  up coming from SDL2:

  > "[conversion] is done during this call [`SDL_GetAudioStreamData`], and `SDL_PutAudioStreamData`
  > simply queues unconverted data for later. This is different than SDL2, where that work was done
  > while inputting new data to the stream."
  > — <https://wiki.libsdl.org/SDL3/SDL_GetAudioStreamData>

- `SDL_SetAudioStreamFormat()` can retarget the **app side** at any time; the **device side cannot be
  changed** while bound ("attempts to change this side will be ignored without an error" —
  <https://wiki.libsdl.org/SDL3/SDL_SetAudioStreamFormat>).
- `SDL_ConvertAudioSamples()` is whole-buffer only and explicitly **must not** be used block-by-block
  ("it will introduce audio artifacts on the boundaries"). Use a stream for anything chunked.
- No `SDL_AUDIO_ALLOW_*` flags exist anymore; format negotiation is the stream's job now
  (`SDL_GetAudioDeviceFormat` tells you what you actually got).

**Measured:** on this machine the default playback device reports `format=0x8010 (SDL_AUDIO_S16LE),
channels=2, freq=44100`. A 48 kHz **mono** source stream bound to it played correctly — SDL did both
the channel upmix and the 48k→44.1k resample transparently. **[measured]**

### 1.5 Queueing / streaming PCM

- `SDL_PutAudioStreamData()` copies and queues; it does **not** block on the device. A single huge
  put is legal but pins memory; the top-up pattern above is what you want.
- Two different counters, do not confuse them:
  - `SDL_GetAudioStreamQueued()` = bytes **put in** (input side, pre-conversion).
  - `SDL_GetAudioStreamAvailable()` = bytes **readable** (output side, post-conversion). Returns
    `-1` on error, not `0` (an SDL2→3 behaviour change).
- `SDL_FlushAudioStream()` = "end of input, drain the resampler now". Legal to put more afterwards,
  but expect a gap. Do **not** call it every frame.
- `SDL_ClearAudioStream()` = hard stop, drops queued data; correct for cutting a voice.
- Because conversion is on read, a stream can legitimately report `queued == 0` while still having
  ~14 bytes of resampler tail — **[measured]**, both voices ended at `queued=14` after drain. A
  "clip ended" test must use `available == 0` (or your own cursor bookkeeping), not `queued == 0`.

### 1.6 Looping a decoded clip

There is **no loop flag anywhere in SDL3's audio API** — no `SDL_AudioStreamLoop`, no
`SDL_PlayAudioStream(..., loops)`. Looping is your cursor logic. Two legitimate patterns:

1. **Frame-driven top-up (recommended).** Keep the decoded PCM in memory, keep a byte cursor, and in
   the per-frame top-up: when `cursor == len`, reset `cursor = 0` if looping, else `SDL_ClearAudioStream`
   + deactivate. Simple, no audio-thread locking, deterministic ordering with the rest of the game.
2. **Get-callback.** `SDL_SetAudioStreamGetCallback()` fires on the audio thread when the device wants
   data:

   > "The callback is not required to obtain all data. It is allowed to read less or none at all."
   > — <https://wiki.libsdl.org/SDL3/SDL_SetAudioStreamGetCallback>

   Use this only if you need sample-accurate gapless looping; it runs on another thread, you must not
   call `SDL_DestroyAudioStream()` from inside it, and shared state needs `SDL_LockAudioStream()`
   (the lock is already held when your callback runs).

For S5's needs (one-shot SFX + a couple of ambient beds), pattern 1 is enough and much easier to
make deterministic for acceptance.

### 1.7 Per-stream and global volume

- **Per voice:** `SDL_SetAudioStreamGain(stream, g)` — `1.0f` = unity, `0.0f` = silence, `>1.0f`
  amplifies (clipping is your problem). Applied during `SDL_GetAudioStreamData`, so it can be
  changed continuously.
- **Global:** `SDL_SetAudioDeviceGain(devid, g)` — **only works on logical devices** opened with
  `SDL_OpenAudioDevice()`; returns `false` on physical device IDs. That is exactly why the
  multi-voice design opens a logical device rather than reusing `SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK`
  everywhere.
- For flattening to a file, `SDL_MixAudio(dst, src, format, len, volume)` (float volume 0…1) replaces
  SDL2's `SDL_MixAudioFormat`/`SDL_MIX_MAXVOLUME`.

### 1.8 Pause / resume

| Intent | Call |
|---|---|
| Pause all audio on the device (pause menu, loading) | `SDL_PauseAudioDevice(devid)` / `SDL_ResumeAudioDevice(devid)` |
| Same, but you only hold the `SDL_AudioStream*` | `SDL_PauseAudioStreamDevice(stream)` / `SDL_ResumeAudioStreamDevice(stream)` |
| Query | `SDL_AudioDevicePaused(devid)` / `SDL_AudioStreamDevicePaused(stream)` |

Semantics per `<https://wiki.libsdl.org/SDL3/SDL_PauseAudioStreamDevice>`: bound streams stop
progressing, nothing is unbound, queued data survives — resume continues where it stopped.

Two gotchas:
- `SDL_OpenAudioDeviceStream()` returns a device that **starts paused**; you must
  `SDL_ResumeAudioStreamDevice()` or nothing plays.
- A device opened with `SDL_OpenAudioDevice()` starts **unpaused** (nothing flows until a stream is
  bound). `SDL_PauseAudioDevice()` also lost its SDL2 `int pause_on` argument.

### 1.9 Enumeration, default-device changes, format changes

```c
int n = 0;
SDL_AudioDeviceID *devs = SDL_GetAudioPlaybackDevices(&n);   /* physical devices only */
for (int i = 0; i < n; i++) SDL_Log("[%u] %s", devs[i], SDL_GetAudioDeviceName(devs[i]));
SDL_free(devs);
```

- `SDL_GetAudioPlaybackDevices()` returns **physical** devices; IDs created by `SDL_OpenAudioDevice()`
  are not in that list. A `NULL` return means error and sets `count = 0`.
- `SDL_GetAudioDeviceName()` works on `SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK` but the docs warn not to
  persist that string: SDL3 **migrates to the new physical device on the fly** when the system default
  changes. Show "System default" in UI.
- Hotplug / format changes arrive as ordinary events:
  `SDL_EVENT_AUDIO_DEVICE_ADDED`, `SDL_EVENT_AUDIO_DEVICE_REMOVED`,
  `SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED`, struct `SDL_AudioDeviceEvent { type, reserved, timestamp,
  which, recording }` (<https://wiki.libsdl.org/SDL3/SDL_AudioDeviceEvent>).
- **Required handling for S5:** on `SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED` (and after a default-device
  migration), re-run `SDL_GetAudioDeviceFormat(devid, …)` and update your "device format" copy used
  for logging/telemetry. **You do not need to rebuild streams** — the device side of a bound stream
  is managed by SDL, and conversion is per-read, so a rate/format change is absorbed automatically.
  Do not try to `SDL_SetAudioStreamFormat()` the device side: it is silently ignored while bound.
- A removed device: SDL closes/migrates; if your logical device is invalidated, recreate it
  (`SDL_OpenAudioDevice` + rebind) — keep the voice PCM buffers independent of the device so this is
  cheap.

### 1.10 Clean shutdown order

```
1. stop producing: stop your top-up loop / clear get-callbacks
2. SDL_UnbindAudioStream(stream) for every voice   (optional; Destroy unbinds)
3. SDL_DestroyAudioStream(stream) for every voice  // frees queued PCM
4. SDL_CloseAudioDevice(devid)                     // only if you opened it with SDL_OpenAudioDevice
5. SDL_QuitSubSystem(SDL_INIT_AUDIO)
6. SDL_Quit()                                      // after window/renderer teardown
```

- `SDL_DestroyAudioStream()` **also closes the device** if the stream came from
  `SDL_OpenAudioDeviceStream()` — do not call `SDL_CloseAudioDevice()` on that device as well.
- Never destroy a stream from inside its own callback ("Do not call `SDL_DestroyAudioStream()` on
  `stream` during this callback").
- `SDL_AudioInit()`/`SDL_AudioQuit()` no longer exist; `SDL_InitSubSystem`/`SDL_QuitSubSystem` are
  refcounted.

### 1.11 SDL2 → SDL3: renamed, removed, changed (audio only)

From `docs/README-migration.md` (`## SDL_audio.h`), <https://wiki.libsdl.org/SDL3/README-migration>:

**Renamed**

| SDL2 | SDL3 |
|---|---|
| `SDL_NewAudioStream` | `SDL_CreateAudioStream` |
| `SDL_AudioStreamPut` | `SDL_PutAudioStreamData` (returns `bool`) |
| `SDL_AudioStreamGet` | `SDL_GetAudioStreamData` |
| `SDL_AudioStreamAvailable` | `SDL_GetAudioStreamAvailable` (returns `-1` on error) |
| `SDL_AudioStreamClear` | `SDL_ClearAudioStream` (returns `bool`) |
| `SDL_AudioStreamFlush` | `SDL_FlushAudioStream` (returns `bool`) |
| `SDL_FreeAudioStream` | `SDL_DestroyAudioStream` |
| `SDL_LoadWAV_RW` | `SDL_LoadWAV_IO` (returns `bool`) |
| `SDL_MixAudioFormat` | `SDL_MixAudio` (returns `bool`) |
| `AUDIO_S16`, `AUDIO_F32`, … | `SDL_AUDIO_S16`, `SDL_AUDIO_F32`, … |
| `SDL_GetAudioDeviceSpec` | `SDL_GetAudioDeviceFormat` |
| `SDL_GetAudioDeviceStatus` | `SDL_AudioDevicePaused` |

**Removed outright**

`SDL_QueueAudio`, `SDL_DequeueAudio`, `SDL_ClearQueuedAudio`, `SDL_GetQueuedAudioSize`,
`SDL_GetNumAudioDevices`, `SDL_OpenAudio`, `SDL_CloseAudio`, `SDL_PauseAudio`,
`SDL_LockAudio`/`SDL_UnlockAudio`, `SDL_LockAudioDevice`/`SDL_UnlockAudioDevice`,
`SDL_AudioCVT`/`SDL_BuildAudioCVT`/`SDL_ConvertAudio`, `SDL_GetDefaultAudioInfo`,
`SDL_AudioInit`/`SDL_AudioQuit`, `SDL_FreeWAV`, all `SDL_AUDIO_ALLOW_*`, and the whole `AUDIO_U16`
family.

**Changed semantics**

- Term "capture" → **"recording"**; "output" → **"playback"**.
- `SDL_OpenAudioDevice(devid, spec)` — no `iscapture`, no `allowed_changes`, no device-name string.
  Devices are opened by **instance ID**.
- `SDL_PauseAudioDevice` lost its second argument.
- `SDL_AudioSpec` shrank to three fields.
- Conversion moved from Put-time to Get-time.
- Audio subsystem is **no longer implicitly initialised** — you must `SDL_Init(SDL_INIT_AUDIO)`.
- Errors are `bool` + `SDL_GetError()`, not `int` codes.

---

## 2. Decode / transcode strategy

### 2.1 What the local data actually looks like  **[local]**

Two things worth recording before anything else, because they change the framing of the question:

1. **The bank magic is `SXDF` / `SXDS`, not `SHDF`.** Reading the first bytes of the extracted
   banks (`$SOTC_GAME_ROOT/sound/shadow/*.sxd*`) gives:
   - `.sxd`, `.sxd1` → `53 58 44 46` = `SXDF`, then version `04 00`
   - `.sxd2` (the huge stream banks) → `53 58 44 53` = `SXDS`, then `04 00`
   `strings` on those banks confirms `SXDF`/`SXDS` plus `TONE`, `REQD`, `NAME`, `WAVE` chunk tags.
   So **`SHDF` in the brief appears to be a transcription of `SXDF`.** Worth fixing in the spec
   before it propagates into a parser.
2. **No codec magic is present in the container at the sampled offsets.** Grepping `com_sys.sxd`
   and `music.sxd1` for `HCA`, `RIFF`, `at9`/`AT9`, `(c)CRI`, `OggS` returned **zero** hits. What is
   present is a `WAVE` chunk table (`WAVE f8 03 00 00 …` followed by per-entry offsets and small
   integer descriptors). That looks like a Bluepoint/Sony bank index whose **codec is described in
   its own descriptor**, not by an embedded third-party magic.
   → **The payload codec is still unidentified.** S5 must treat "which codec" as an open question
   with a concrete decision tree, not an assumption.

Bank sizes for context: `music.sxd2` ≈ 172 MB, `str_bgm.sxd2` ≈ 257 MB, `str_amb.sxd2` ≈ 358 MB,
`str_event.sxd2` ≈ 366 MB (≈ 1.1 GB of streamed audio, 42 files in `sound/shadow`).

### 2.2 Codec matrix — what FFmpeg can decode, and under what licence

All entries verified against upstream `master` sources, not from memory.

| Codec | FFmpeg component | Exists? | Licence of the file(s) | Notes |
|---|---|---|---|---|
| **ATRAC9** | `libavcodec/atrac9dec.c` → decoder `atrac9` | **Yes** | **LGPL-2.1+** (file header states LGPL v2.1 or later) | Landed 2018-06-30 ("lavc: implement an ATRAC9 decoder") → first shipped in **FFmpeg 4.1** (`Changelog`). Decoder only, **no encoder**. |
| **CRI HCA** | `libavcodec/hcadec.c` (decoder `hca`) + `libavformat/hca.c` (demuxer `hca`) | **Yes** | **LGPL-2.1+** | Landed 2020-03-15 ("avcodec: add CRI HCA decoder", "avformat: add hca demuxer") → first in **FFmpeg 4.3**. Supports encrypted HCA via private options `hca_lowkey`, `hca_highkey`, `hca_subkey`. |
| **CRI ADX** | `libavcodec/adxdec.c` (`adpcm_adx`) + `libavformat/adxdec.c` (demuxer `adx`) | **Yes** | **LGPL-2.1+** | Ancient and stable; `Changelog` lists "CRI ADX audio format muxer and demuxer". |
| **MP3** | `mp3` / `mp3float` native decoder, `mp3` demuxer | **Yes** | **LGPL-2.1+** (native); `libmp3lame` is LGPL too | `mp3_demuxer_select="mpegaudio_parser"`. |
| **Vorbis** | native `vorbis` decoder + `ogg` demuxer (`libvorbis` optional) | **Yes** | **LGPL-2.1+** | Best-supported lossy option; Ogg magic `OggS` was **not** found in the sampled banks. |
| **PCM** | `pcm_s16le`/`pcm_s24le`/`pcm_f32le` … | **Yes** | LGPL | Trivial; also what SDL consumes natively. |
| **ADPCM (generic)** | `adpcm_ima_*`, `adpcm_ms`, `adpcm_psx`, `adpcm_yamaha` … | **Yes** | LGPL | Covers most "ADPCM" variants a PS4-era title might use, except Sony's own ATRAC family. |
| **ATRAC3 / ATRAC3plus** | `atrac3`, `atrac3p`, `oma` demuxer | **Yes** | LGPL | Present but not expected here (that's PSP/PS3-era). |

No `configure` dependency lines exist for `atrac9`, `hca`, or `adx` — they are **pure native,
dependency-free** components, which means they are in *every* normal FFmpeg build (the usual Homebrew
`ffmpeg` includes them). Verified: `grep -E "^(atrac9|hca|adx)_" configure` → no matches.

### 2.3 Licensing, precisely

- Default FFmpeg is **LGPL-2.1-or-later**. `--enable-gpl` switches the *build* to GPL-2+; `--enable-nonfree`
  makes the result unredistributable. The three codecs above are LGPL regardless.
- **This project is already GPL-3.0** (`AGENTS.md`, hard constraints), and S5 is **personal use, not
  redistributed**. Under either an LGPL or GPL FFmpeg build there is no conflict: LGPL-2.1+ may be
  upgraded to GPL-3, and a GPL-3 build links fine into a GPL-3 work.
- **The real constraint is architectural, not legal:** `AGENTS.md` / design §8 say *"the runtime
  never parses original game containers."* Therefore FFmpeg (or any decoder) belongs **in the .NET
  converter, at build time**, producing PCM/WAV/glTF-embedded audio. The C++ viewer must never link
  FFmpeg nor open an `.sxd`.
- Also note the pre-existing project rule shape: the Oodle dylib and ImageSharp are "personal use
  only — never redistribute" (`tools/native/NOTICE.md`). If FFmpeg is added as a build dependency,
  it should get the same treatment: a `NOTICE.md` entry, no binary checked in, no codec library
  shipped.
- ATRAC9/HCA are **patented** formats. Decoding for personal use is not a licensing event; shipping a
  decoder is. Another reason to keep this build-time and unredistributed.

### 2.4 Verdict if the SHDF/SXDF payload is ATRAC9

**Primary: FFmpeg `atrac9`, at build time, invoked by the .NET converter.**

- It is the only mature ATRAC9 decoder, it is LGPL, dependency-free, and already present in a stock
  `ffmpeg` binary on this machine.
- Mechanics: ATRAC9 lives inside RIFF/WAVE. FFmpeg's `riff.c` already maps the ATRAC9 **WAVE_FORMAT_EXTENSIBLE
  subformat GUID** `D242E147-BA36-8D4D-88FC-61654F8C836C` to `AV_CODEC_ID_ATRAC9`, so a well-formed
  `.at9` (or a `WAVE` chunk you can re-wrap as RIFF) is decodable with a plain
  `ffmpeg -i clip.at9 -f wav out.wav`.
- **Caveat that will bite:** `atrac9` needs **extradata** (block/config info that normally comes from
  the RIFF `fmt ` chunk). If the SXDF `WAVE` descriptor holds the payload **without** a RIFF header,
  you must synthesise a minimal RIFF/WAVE header (or feed `-extradata`) before ffmpeg will accept it.
  Budget a small `At9Wrapper` step in the converter: descriptor → RIFF header → ffmpeg.
- Command shape for the converter: `ffmpeg -v error -i <wrapped.at9> -map_metadata -1 -ac 2 -ar 48000
  -c:a pcm_s16le <out.wav>` — a single, explicit format (48 kHz stereo S16) so the viewer's SDL path
  needs no resampling at all and mixdowns are bit-comparable.

**Alternative with no FFmpeg dependency (worth considering for a .NET-10 converter):**

| Option | Licence | Fit |
|---|---|---|
| **LibAtrac9** (`Thealexbarney/LibAtrac9`) | **MIT** | C **and C#** ATRAC9 decoder, purpose-built. Cleanest if you want zero native deps. |
| **VGAudio** (`Thealexbarney/VGAudio`) | **MIT**, `netstandard1.1` | Pure-managed C# decoders for **Atrac9, CriAdx, CriHca**, plus PCM. Consumable from .NET 10 as-is (last commit 2023). One dependency covers three of the five candidate codecs — strongest single option for `BpConvert`. |
| **vgmstream** | permissive (GitHub reports `NOASSERTION`; custom MIT-style wording) | Reference implementation for HCA/ADX/AT9/PS4 variants; best used as an oracle to validate your output, not as a linked library. |

**Recommendation:** try `VGAudio` first inside the converter (managed, MIT, no subprocess, covers
ATRAC9+HCA+ADX); keep `ffmpeg` as the *oracle* in the acceptance script to cross-check a handful of
decoded clips (`ffmpeg -i x.wav -f md5 -`). If VGAudio's ATRAC9 path fails on these payloads, fall
back to the FFmpeg subprocess route in §2.4 — it is the same output contract either way.

### 2.5 If no decoder exists (the fallback ladder)

Ordered, cheapest-to-most-expensive. The rule throughout: **never silently substitute audio**; the
converter's catalog must record the truth.

1. **Identify precisely first.** Dump the `WAVE` descriptor of one small bank (`com_sys.sxd`, 1.07 MB,
   has plenty of one-shots) and read the descriptor integers (`… 01 00 00 00 21 02 ff 00 80 bb 00 00
   66 26 00 00` region). One afternoon with a hexdump may turn this from "unknown" into "ATRAC9 with a
   96-byte custom header".
2. **Wrap and retry.** Most "undecodable" PS4 audio is a raw codec payload missing its header. Try
   RIFF-wrapping as ATRAC9, then as `WAVE_FORMAT_EXTENSIBLE` PCM, and let `ffprobe -show_streams`
   tell you what it thinks it is.
3. **Try the sibling files.** `.sxd2`/`.sxd1` are stream banks; a codec that fails on the tiny SFX
   bank may decode fine on the music bank (different bitrate/config), which tells you the container
   logic is right and the config parser is wrong.
4. **Record it as undecoded, do not fake it.** Follow the existing M0 precedent ("BC6H / `CC0A`
   textures are refused by name and skipped; the model still converts"): the sound bank converts,
   that clip is written to the catalog as `codec: unknown, status: refused`, and the viewer plays
   silence for that event. The library stays complete and honest.
5. **Only for the acceptance harness**, synthesise a placeholder tone named `<clip>.placeholder.wav`,
   clearly distinguished in the manifest. It exists so S5's timing/mixing code can be tested before
   the decode question is closed — never shipped as if it were the real sound.

---

## 3. Offline verification (no physical device)

### 3.1 The dummy driver

```bash
SDL_AUDIO_DRIVER=dummy ./app/build/sotc-viewer ...
# or, in-process, BEFORE SDL_Init:
SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
```

- `SDL_HINT_AUDIO_DRIVER` ("a variable that specifies an audio backend to use … should be set before
  SDL is initialized") — <https://wiki.libsdl.org/SDL3/SDL_HINT_AUDIO_DRIVER>.
- The SDL2 env name `SDL_AUDIODRIVER` was renamed to **`SDL_AUDIO_DRIVER`**; the old name still works
  as a fallback (`README-migration.md`, `SDL_hints.h`).
- `SDL_GetCurrentAudioDriver()` returns `"dummy"` — cheap assertion that the run really is headless.
- Dummy pacing: by default it **simulates real time**, so a 3-second sound takes 3 seconds.
  `SDL_HINT_AUDIO_DUMMY_TIMESCALE` ("adjust this rate higher or lower down to 0, default 1.0")
  removes that — set `SDL_AUDIO_DUMMY_TIMESCALE=0`.
  <https://wiki.libsdl.org/SDL3/SDL_HINT_AUDIO_DUMMY_TIMESCALE>
- **[measured]** `SDL_AUDIO_DRIVER=dummy` works with this Homebrew build; the default device reports
  44.1 kHz stereo S16; `SDL_GetAudioPlaybackDevices()` returned 1 entry ("System audio playback
  device") even headless. `SDL_AUDIO_DUMMY_TIMESCALE=0` removed the driver's delay — after that the
  *app's* own pacing loop dominates wall time, so do not expect a real-time sound to finish instantly
  just because the driver is fast.

**Verdict:** the dummy driver is the right tool for a **smoke test** ("audio subsystem initialises,
voices bind, no crash, no SDL error"), not for acceptance numbers. It produces no output to inspect.

### 3.2 The disk driver — tempting, but not deterministic

`SDL_AUDIO_DRIVER=disk` writes the **device mix** to a file:

```bash
SDL_AUDIO_DRIVER=disk \
SDL_AUDIO_DISK_OUTPUT_FILE=/tmp/mix.raw \
SDL_AUDIO_DISK_TIMESCALE=0 \
./app/build/sotc-viewer ...
```

Hints: `SDL_AUDIO_DISK_OUTPUT_FILE`, `SDL_AUDIO_DISK_TIMESCALE`, `SDL_AUDIO_DISK_INPUT_FILE`
(replacements for SDL2's `SDL_DISKAUDIOFILE`/`SDL_DISKAUDIODELAY` — `README-migration.md`).

Two facts that matter:

1. **It writes raw PCM, not WAV.** **[measured]** the driver logs
   `Writing to file [/tmp/a.raw], format=S16LE channels=2 freq=44100` and emits no RIFF header
   (`DISKDEFAULT_OUTFILE "sdlaudio.raw"` in `src/audio/disk/SDL_diskaudio.c`). You would have to wrap
   it yourself.
2. **The byte count is wall-clock dependent, so it is not reproducible.** **[measured]** two runs of
   the *same* deterministic tone generator, same code, produced **693,714,944** and **462,761,984**
   bytes in ~1.2 s each (with `SDL_AUDIO_DISK_TIMESCALE=0` the driver free-runs and the amount written
   is however many device callbacks the scheduler allowed). SHA-256 therefore differs:
   `43d9055d…` vs `71feb97a…`.

→ **The disk driver is a debugging aid, not an acceptance mechanism.** Any hash-of-the-mix acceptance
test built on it will flake.

### 3.3 Recommended: an in-process mixdown-to-WAV path

**Yes — a mixdown-to-WAV path is strictly better for deterministic acceptance**, and it is cheap
because you need the mixer anyway.

Design:

```
sotc-viewer --s5-mixdown out/s5/mix.wav --s5-ticks 240 [--s5-fixed-tick 1/60]
```

- **Never calls `SDL_Init(SDL_INIT_AUDIO)`** in this mode. Voices are mixed in your own float buffer
  at a fixed rate (48 kHz stereo), stepping the *same* game clock the battle script uses.
- Use `SDL_AudioStream` with an explicit `src`/`dst` spec for any conversion you need (e.g. a 44.1 kHz
  clip into the 48 kHz master) — it is deterministic given the same input and same SDL build.
  `SDL_MixAudio()` (float volume) does the summing, or just accumulate in `float`.
- Write the WAV yourself (44-byte RIFF header) — SDL3 has **no WAV writer** (`SDL_LoadWAV` is
  read-only). ~30 lines.
- Assertions on the artefact, not on adjectives:
  - SHA-256 of the `data` chunk (regression pin per milestone).
  - Per-event peak/RMS windows (e.g. "the strike SFX window has peak > -6 dBFS and the quiet window
    < -60 dBFS").
  - Clip start offsets in samples (catches scheduling regressions that a hash would flag but not
    explain).
  - Duration == `ticks × tick`, exactly — no driver, so no drift.
- Keep **one** device-backed test as a smoke test (`SDL_AUDIO_DRIVER=dummy
  SDL_AUDIO_DUMMY_TIMESCALE=0`, assert `SDL_GetCurrentAudioDriver() == "dummy"`, no `SDL_GetError()`).

**Measured sanity check that this is achievable:** binding two independently-gauged streams
(`gain 0.25` and `0.10`) plus a device gain of `0.8` to one logical device, with a 48 kHz mono source
against a 44.1 kHz stereo device, played correctly with no resampling code on our side — i.e. the
runtime path is genuinely "give SDL your clip format". **[measured]**

Per repo convention, wrap every viewer/acceptance run in `caffeinate -dimsu`, and never run two
acceptances concurrently.

---

## 4. Open questions to close before S5 implementation

1. **`SHDF` vs `SXDF`** — confirm the magic for the spec (`SXDF` for `.sxd`/`.sxd1`, `SXDS` for
   `.sxd2` per the extracted tree). Fix the name everywhere before a parser is written.
2. **Codec identification** — decode the `WAVE` chunk descriptor of `com_sys.sxd` (smallest bank,
   named one-shots). This single answer picks the §2.4 path vs the §2.5 fallback.
3. **Scope of S5** — one-shots only (footsteps, strike, roar) vs. music/ambience streaming
   (`music.sxd2` is 172 MB; do not load it whole). This determines whether the voice layer needs
   streaming file sources or just in-memory clips.
4. **Do the .sxd banks store per-clip sample rate?** If not, all clips are one fixed rate, which
   simplifies the converter and the mixer.
5. **Loop points** — the bank may carry explicit loop markers (`TONE` chunk?); if it does, use them
   rather than whole-clip looping.

---

## 5. Sources

**SDL3 (Context7: `/libsdl-org/sdlwiki`, i.e. the SDL wiki; plus upstream `libsdl-org/SDL` sources)**

- Migration guide (authoritative SDL2→SDL3 audio diff): <https://wiki.libsdl.org/SDL3/README-migration>
- Audio category overview: <https://wiki.libsdl.org/SDL3/CategoryAudio>
- `SDL_OpenAudioDeviceStream`: <https://wiki.libsdl.org/SDL3/SDL_OpenAudioDeviceStream>
- `SDL_CreateAudioStream` / `SDL_SetAudioStreamFormat`: <https://wiki.libsdl.org/SDL3/SDL_SetAudioStreamFormat>
- `SDL_BindAudioStreams` (mixing contract): <https://wiki.libsdl.org/SDL3/SDL_BindAudioStreams>
- `SDL_PutAudioStreamData` / `SDL_GetAudioStreamData`: <https://wiki.libsdl.org/SDL3/SDL_PutAudioStreamData>,
  <https://wiki.libsdl.org/SDL3/SDL_GetAudioStreamData>
- `SDL_GetAudioStreamQueued` vs `Available`: <https://wiki.libsdl.org/SDL3/SDL_GetAudioStreamQueued>
- `SDL_FlushAudioStream`, `SDL_ClearAudioStream`: <https://wiki.libsdl.org/SDL3/SDL_FlushAudioStream>,
  <https://wiki.libsdl.org/SDL3/SDL_ClearAudioStream>
- Gain: <https://wiki.libsdl.org/SDL3/SDL_SetAudioStreamGain>,
  <https://wiki.libsdl.org/SDL3/SDL_SetAudioDeviceGain>
- Pause/resume: <https://wiki.libsdl.org/SDL3/SDL_PauseAudioStreamDevice>,
  <https://wiki.libsdl.org/SDL3/SDL_ResumeAudioStreamDevice>,
  <https://wiki.libsdl.org/SDL3/SDL_AudioDevicePaused>
- Devices: <https://wiki.libsdl.org/SDL3/SDL_GetAudioPlaybackDevices>,
  <https://wiki.libsdl.org/SDL3/SDL_GetAudioDeviceFormat>,
  <https://wiki.libsdl.org/SDL3/SDL_GetAudioDeviceName>,
  <https://wiki.libsdl.org/SDL3/SDL_AudioDeviceEvent>
- Callbacks: <https://wiki.libsdl.org/SDL3/SDL_AudioStreamCallback>,
  <https://wiki.libsdl.org/SDL3/SDL_SetAudioStreamGetCallback>
- Offline helpers: <https://wiki.libsdl.org/SDL3/SDL_ConvertAudioSamples>,
  <https://wiki.libsdl.org/SDL3/SDL_LoadWAV>, <https://wiki.libsdl.org/SDL3/SDL_MixAudio>
- Teardown: <https://wiki.libsdl.org/SDL3/SDL_DestroyAudioStream>
- Hints: <https://wiki.libsdl.org/SDL3/SDL_HINT_AUDIO_DRIVER>,
  <https://wiki.libsdl.org/SDL3/SDL_HINT_AUDIO_DUMMY_TIMESCALE>,
  <https://wiki.libsdl.org/SDL3/EnvironmentVariables>
- Driver source (dummy/disk behaviour): `src/audio/dummy/SDL_dummyaudio.c`,
  `src/audio/disk/SDL_diskaudio.c` in <https://github.com/libsdl-org/SDL>
- Header: `include/SDL3/SDL_audio.h` in <https://github.com/libsdl-org/SDL>

**FFmpeg (Context7: `/websites/ffmpeg_doxygen_trunk`; plus upstream sources)**

- `libavcodec/atrac9dec.c` (LGPL-2.1+ header, "ATRAC9 decoder", 2018-06-30):
  <https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/atrac9dec.c>
- `libavcodec/hcadec.c` + `libavformat/hca.c` (LGPL-2.1+, 2020-03-15):
  <https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/hcadec.c>,
  <https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/hca.c>
- `libavcodec/adxdec.c` (<https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/adxdec.c>)
- Registration: `libavcodec/allcodecs.c` (`ff_atrac9_decoder`, `ff_hca_decoder`,
  `ff_adpcm_adx_decoder`), `libavformat/allformats.c` (`ff_hca_demuxer`, `ff_adx_demuxer`)
- ATRAC9 WAVE_FORMAT_EXTENSIBLE GUID mapping: `libavformat/riff.c` line ~665
- Version introduction: `Changelog` — "CRI HCA decoder/demuxer" under **version 4.3**,
  "ATRAC9 decoder" under **version 4.1**
- Encryption keys for HCA: private options `hca_lowkey`, `hca_highkey`, `hca_subkey` (`libavformat/hca.c`)
- Licensing: <https://ffmpeg.org/legal.html>

**Alternative decoders**

- LibAtrac9 (MIT, C + C#): <https://github.com/Thealexbarney/LibAtrac9>
- VGAudio (MIT, `netstandard1.1`; `Atrac9`, `CriAdx`, `CriHca`, `Pcm8`):
  <https://github.com/Thealexbarney/VGAudio>
- vgmstream (permissive, custom wording; reference/oracle):
  <https://github.com/vgmstream/vgmstream>

**Local evidence**

- `$SOTC_GAME_ROOT/sound/shadow/*.sxd*` — magic `SXDF`/`SXDS`, `TONE`/`REQD`/`NAME`/`WAVE` chunks,
  42 files ≈ 1.1 GB. Read-only sampling; no file was modified.
- `.superpowers/sdd/s5-audio/` — this directory.
