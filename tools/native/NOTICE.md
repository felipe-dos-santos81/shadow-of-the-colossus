# Native decoder: `liboo2core_8_win64_.dylib`

- **What:** RAD **Oodle 2.9.16** SDK runtime, macOS universal binary (x86_64 + arm64),
  1,206,880 bytes. Original upstream file name: `liboo2coremac64.2.9.16.dylib`.
- **Why:** BluePoint game containers (`8C 0A` / `8C 06` CMSH/CMDL/... and CTXR) are Oodle-compressed.
  `AquaModelLibrary.Data`'s decompression path (`Zamboni.Oodle.OodleDecompress`, see
  `third_party/PSO2-Aqua-Library/ZamboniLib/ZamboniLib/Oodle.cs`) P/Invokes the symbol
  `OodleLZ_Decompress` from the library name `oo2core_8_win64_`. On macOS that name is resolved to
  this file, which is copied next to the built binaries by
  `src/BpConvert.Core/BpConvert.Core.csproj` (the `Content` item). The `_win64_` part is just the
  library's hard-coded P/Invoke string; the binary here is the official macOS build.
- **Provenance:** `WorkingRobot/OodleUE` GitHub mirror of the Oodle SDK that ships inside Epic's
  Unreal Engine (`Engine/Source/Runtime/OodleDataCompression`). Obtained 2026-10-15 from the
  local tooling copy at `../../SOTC-extract/tools/oodleue/` (source zip + dylib).
- **sha256:** `47269bc3c9f7ef7948775be2c018ce674d1ab8a8526b6935e9d1033b0b47e8b7`
- **Terms:** Oodle is proprietary (RAD Game Tools / Epic Games), distributed under the Unreal
  Engine EULA. **Personal use only — do not redistribute** this binary or any build product that
  embeds it. This repository is a personal fan project; nothing is published with this file.
- **Behavior note:** `OodleLZ_Decompress` requires `rawLen` to equal the **exact** decompressed
  size; a larger output buffer fails. The library passes exactly the size read from the container's
  12-byte footer trailer, so it is compatible as-is.

## Managed dependency: `SixLabors.ImageSharp`

- SixLabors.ImageSharp 3.1.6 is pulled in transitively through the GPL-3.0 submodule
  (`AquaModelLibrary.Data` carries it as a direct `PackageReference`) and is **present in the
  product graph** — `SixLabors.ImageSharp.dll` ships in the published CLI output (`out/bin/`).
- **Terms:** Six Labors Split License (not Apache-2.0). As with the Oodle dylib above: personal
  use only — do not redistribute this repository's build output.
