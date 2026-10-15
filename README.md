# sotc-native

Native Apple-Silicon rebuild of **Shadow of the Colossus (2018)** from the extracted PS4 game data.

- Design: [`docs/superpowers/specs/2026-10-15-sotc-native-rebuild-design.md`](docs/superpowers/specs/2026-10-15-sotc-native-rebuild-design.md)
- Game data (read-only, not in repo): `../SOTC-extract/extracted/game/`
- Status: M0 (converter toolchain) in progress.

## Toolchain (bp-convert)

.NET 10 solution: `bp-convert` CLI over `BpConvert.Core`.

```bash
dotnet build BpConvert.sln
export SOTC_GAME_ROOT=/path/to/extracted/game
dotnet test tests/BpConvert.Tests -v q
```

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
