# sotc-native

Native Apple-Silicon rebuild of **Shadow of the Colossus (2018)** from the extracted PS4 game data.

- Design: [`docs/superpowers/specs/2026-10-15-sotc-native-rebuild-design.md`](docs/superpowers/specs/2026-10-15-sotc-native-rebuild-design.md)
- Game data (read-only, not in repo): `../SOTC-extract/extracted/game/`
- Status: **M0 (converter toolchain) — acceptance pass run**: 10-character pre-flight + largest
  model check + the full `cmsh` batch, recorded in
  [`docs/notes/m0-results.md`](docs/notes/m0-results.md). M1 (Godot review scene) comes next.

## Toolchain (bp-convert)

.NET 10 solution: `bp-convert` CLI over `BpConvert.Core`.

```bash
dotnet build BpConvert.sln
export SOTC_GAME_ROOT=/path/to/extracted/game
dotnet test tests/BpConvert.Tests -v q
```

### Batch conversion

```bash
dotnet publish src/BpConvert.Cli -c Release -o out/bin
dotnet exec out/bin/bp-convert.dll convert --root "$SOTC_GAME_ROOT" --out out/library \
  --under models --under characters --ext cmsh --jobs 8   # 16,782 sources; resumable
dotnet exec out/bin/bp-convert.dll report --out out/library --top 20
```

`out/library/catalog.json` records every source (status, glb, pngs, meshes, vertices, error) and is
flushed atomically while the batch runs, so an interrupted run resumes where it stopped: entries
already `ok` are skipped, entries recorded `failed` are retried. `scripts/m0-acceptance.sh` wraps
the whole M0 acceptance pass (10-character pre-flight with a SharpGLTF load assertion, largest-model
check, publish, background batch launch, convergence re-run) and
[`docs/notes/m0-results.md`](docs/notes/m0-results.md) is its result sheet.

Invoke the published CLI as `dotnet exec out/bin/bp-convert.dll`: the bare apphost
(`out/bin/bp-convert`) cannot launch on this machine without `DOTNET_ROOT` (Homebrew .NET quirk,
[`docs/notes/aqua-core-api.md`](docs/notes/aqua-core-api.md) §0).

**M0 limitations.** The batch converts `cmsh` only — converting a `.cmdl` *as a source* is not
implemented (a sibling `.cmdl` is read as the material map of its `.cmsh`); cmdl-driven model
assembly is an M1 item. A texture the decoder does not support (notably BC6H/HDR) is refused by
name and skipped, not fatal: the model still converts, the texture is simply absent from the
output. A few `cmsh` (grass-family vertex layouts) hit a parse bug in the vendored
AquaModelLibrary — see §5 of the results sheet.

- `third_party/PSO2-Aqua-Library` (GPL-3.0, pinned submodule, parse-only);
  `third_party/Directory.Build.props` lets its `.NET Framework 4.8` helper project build on macOS.
- `tools/native/liboo2core_8_win64_.dylib` — official RAD Oodle 2.9.16 decoder (macOS universal),
  vendored so Oodle-compressed game data can be read. **Personal use only — never redistribute**
  (Unreal Engine EULA); see [`tools/native/NOTICE.md`](tools/native/NOTICE.md). It is copied next to
  the built binaries automatically.
- Library API notes the converter is written against:
  [`docs/notes/aqua-core-api.md`](docs/notes/aqua-core-api.md) — section 0 records the gate outcome
  (link target `AquaModelLibrary.Data`, vendored decoder, decode semantics) and the verified samples.

Personal-use fan project. No game assets are distributed in this repository.
