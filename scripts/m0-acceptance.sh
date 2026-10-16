#!/usr/bin/env bash
#
# m0-acceptance.sh — the M0 acceptance pass for bp-convert, re-runnable end to end.
#
# Steps (design §9: "Wander and >=9 other characters convert reproducibly; catalog covers
# characters/ + models/"):
#
#   1. pre-flight — convert the 10 pinned characters (models/shadow/characters/<name>, cmsh only)
#      into the shared library, then assert every produced .glb loads with SharpGLTF and every
#      requested group carries at least one mesh;
#   2. largest-model check — convert the largest .cmsh under the colossus_01_minotaur_a statue
#      _cmn/ (the pinned Review Focus #4 sample) and assert its .glb loads;
#   3. publish + full batch — `dotnet publish` to out/bin, then (unless a previous full run
#      already finished; see below) launch the pinned batch in the background, logging to
#      out/m0-run.log:
#        --under models --under characters --ext cmsh --jobs 8
#      The run is resumable: rerunning picks up from the catalog where it stopped;
#   3'. convergence — when out/m0-run.log already carries a completed-batch summary, the script
#      does not relaunch the batch: it re-invokes the same scope in the foreground and fails the
#      acceptance if that second invocation converts anything (it must report `converted: 0`).
#
# `--ext cmsh` only: converting a .cmdl *as a source* is not implemented (sibling .cmdls are
# consumed as material maps by the adapter); cmdl-driven model assembly is an M1 item
# (T7 amendment, 2026-10-16).
#
# The acceptance checker is generated (not committed) into out/glbcheck/: a tiny program pinned to
# SharpGLTF.Core 1.0.7 (the test project's pin; restored from the local NuGet cache) that reads
# out/library/catalog.json, loads each recorded-ok glb below the requested prefixes, and
# cross-checks the converter-reported meshes/vertices against what the file actually carries.
# Nothing below src/ or tests/ is touched.
#
# Environment:
#   SOTC_GAME_ROOT  required — the extracted game tree (read-only)
#   OUT_LIB         output library (default: <repo>/out/library)
#   JOBS            conversion parallelism (default: 8)
#
# Safety rails: a second concurrent batch is refused via out/m0-run.pid, and the disk guard refuses
# to launch the batch when out/library is already over 35 GB or the volume has under 10 GB free.
# Every acceptance number in docs/notes/m0-results.md comes from a run of this script (or from its
# exact commands, recorded verbatim).

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${SOTC_GAME_ROOT:?set SOTC_GAME_ROOT to the extracted game tree}"
out_lib="${OUT_LIB:-$repo_root/out/library}"
jobs="${JOBS:-8}"
bin_dir="$repo_root/out/bin"
log="$repo_root/out/m0-run.log"
conv_log="$repo_root/out/m0-convergence.log"
pid_file="$repo_root/out/m0-run.pid"
checker_dir="$repo_root/out/glbcheck"
checker_dll="$checker_dir/bin/Release/net10.0/glbcheck.dll"

# Pinned acceptance scope: wander + 9 other characters (design §9: "Wander and >=9 other
# characters"). Characters whose cmsh set is tiny or absent are deliberately avoided here; these
# ten cover the humanoids, a horse, a bird, and six colossi variants.
chars=(
  models/shadow/characters/wander
  models/shadow/characters/agro
  models/shadow/characters/mono
  models/shadow/characters/girl_b
  models/shadow/characters/hawk
  models/shadow/characters/colossus_01_minotaur_a
  models/shadow/characters/colossus_04_kirin
  models/shadow/characters/colossus_09_desert_tortoise
  models/shadow/characters/colossus_14_cerberus
  models/shadow/characters/colossus_16_mega_maid
)
statue_dir="models/shadow/objects/actors/statues/colossus_01_minotaur_a/_cmn"

library_budget_kb=$((35 * 1024 * 1024))  # 35 GB in KB — stop launching when exceeded
volume_floor_kb=$((10 * 1024 * 1024))    # require >= 10 GB free before launching

stamp() { printf '%s %s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')" "$*"; }

batch_running() {
  [[ -f "$pid_file" ]] || return 1
  local pid
  pid="$(cat "$pid_file" 2>/dev/null || true)"
  [[ -n "$pid" ]] || return 1
  kill -0 "$pid" 2>/dev/null
}

# --- Guard: never publish over (or touch the catalog of) a running batch ------

if batch_running; then
  echo "m0-acceptance: a batch is already running (pid $(cat "$pid_file")); refusing to run" >&2
  echo "m0-acceptance: monitor with: wc -l '$log'; rerun this script once it has finished" >&2
  exit 1
fi

# --- Step 3a: publish ---------------------------------------------------------

echo "m0-acceptance: publish -> $bin_dir"
dotnet publish "$repo_root/src/BpConvert.Cli" -c Release -o "$bin_dir" -v q --nologo

# The published apphost (out/bin/bp-convert) cannot run on this machine without DOTNET_ROOT
# (Homebrew .NET apphost quirk, docs/notes/aqua-core-api.md §0); `dotnet exec` on the published dll
# sidesteps it. Fall back to the source tree if the publish output is missing.
if [[ -f "$bin_dir/bp-convert.dll" ]]; then
  cli=(dotnet exec "$bin_dir/bp-convert.dll")
else
  cli=(dotnet run --project "$repo_root/src/BpConvert.Cli" -c Release --)
fi

# --- The generated acceptance checker ----------------------------------------

mkdir -p "$checker_dir"
cat > "$checker_dir/glbcheck.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>Exe</OutputType>
    <TargetFramework>net10.0</TargetFramework>
    <Nullable>enable</Nullable>
    <ImplicitUsings>enable</ImplicitUsings>
  </PropertyGroup>
  <ItemGroup>
    <!-- The test project's pin; resolved from the local NuGet cache (offline-friendly). -->
    <PackageReference Include="SharpGLTF.Core" Version="1.0.7" />
  </ItemGroup>
</Project>
EOF

cat > "$checker_dir/Program.cs" <<'EOF'
// glbcheck — M0 acceptance checker for converted libraries.
//
// Reads <lib>/catalog.json, loads every recorded-ok .glb below the requested source prefixes with
// SharpGLTF (the same loader the test suite pins), cross-checks the converter-reported
// meshes/vertices against what the glb actually carries, and prints per-group aggregates.
// One group per --prefix (a prefix with no recorded entries is a failure); a prefix may name a
// directory or a single source file, and the first matching prefix owns the source (so keep nested
// prefixes out of one invocation). Without --prefix the whole catalog is checked, grouped by the
// source's first --depth path components.
// Exit codes: 0 = every checked glb loaded and every requested group has files and meshes,
// 1 = at least one failure (load error, missing glb, empty group, 0-mesh group under
// --require-meshes), 2 = bad invocation.
using System.Text.Json;
using SharpGLTF.Schema2;

string? lib = null;
var depth = 4;
var requireMeshes = false;
var prefixes = new List<string>();

for (var i = 0; i < args.Length; i++)
{
    switch (args[i])
    {
        case "--lib" when i + 1 < args.Length:
            lib = args[++i];
            break;
        case "--depth" when i + 1 < args.Length && int.TryParse(args[i + 1], out var d) && d > 0:
            depth = d;
            i++;
            break;
        case "--prefix" when i + 1 < args.Length:
            prefixes.Add(args[++i].TrimEnd('/'));
            break;
        case "--require-meshes":
            requireMeshes = true;
            break;
        default:
            Console.Error.WriteLine("usage: glbcheck --lib <lib> [--depth N] [--require-meshes] [--prefix <source dir>]...");
            return 2;
    }
}

if (lib is null)
{
    Console.Error.WriteLine("glbcheck: --lib <lib> is required");
    return 2;
}

var catalogPath = Path.Combine(lib, "catalog.json");
if (!File.Exists(catalogPath))
{
    Console.Error.WriteLine($"glbcheck: no catalog at {catalogPath}");
    return 2;
}

var failures = 0;
if (requireMeshes && prefixes.Count == 0)
{
    Console.Error.WriteLine("glbcheck: --require-meshes needs at least one --prefix");
    return 2;
}

var groups = new SortedDictionary<string, Group>(StringComparer.Ordinal);
foreach (var prefix in prefixes)
{
    groups[prefix] = new Group();
}

int files = 0, loaded = 0, failed = 0, mismatches = 0;
long totalMeshes = 0, totalVertices = 0, totalBytes = 0;

using var document = JsonDocument.Parse(File.ReadAllText(catalogPath));
foreach (var property in document.RootElement.GetProperty("entries").EnumerateObject())
{
    var source = property.Name;
    var entry = property.Value;
    if (entry.GetProperty("status").GetString() != "ok")
    {
        continue;
    }
    var matchedPrefix = prefixes.FirstOrDefault(p => source == p || source.StartsWith(p + "/", StringComparison.Ordinal));
    if (prefixes.Count > 0 && matchedPrefix is null)
    {
        continue;
    }

    files++;
    var glbRel = entry.GetProperty("glb").GetString()!;
    var glbPath = Path.Combine(lib, glbRel.Replace('/', Path.DirectorySeparatorChar));
    var catalogMeshes = entry.GetProperty("meshes").GetInt32();
    var catalogVertices = entry.GetProperty("vertices").GetInt64();

    var key = matchedPrefix ?? string.Join('/', source.Split('/').Take(depth));
    var group = groups.TryGetValue(key, out var g) ? g : (groups[key] = new Group());

    var bytes = File.Exists(glbPath) ? new FileInfo(glbPath).Length : -1;
    string outcome;
    int glbMeshes = -1;
    long glbVertices = -1;
    try
    {
        if (bytes < 0)
        {
            throw new FileNotFoundException("recorded glb is missing", glbPath);
        }

        var model = ModelRoot.Load(glbPath);
        glbMeshes = model.LogicalMeshes.Count;
        glbVertices = 0;
        foreach (var mesh in model.LogicalMeshes)
        {
            foreach (var primitive in mesh.Primitives)
            {
                glbVertices += primitive.VertexAccessors.TryGetValue("POSITION", out var accessor) ? accessor.Count : 0;
            }
        }

        loaded++;
        var matches = glbMeshes == catalogMeshes && glbVertices == catalogVertices;
        if (!matches)
        {
            mismatches++;
        }
        outcome = matches ? "ok" : "mismatch";
    }
    catch (Exception e)
    {
        failed++;
        failures++;
        outcome = $"FAILED {e.GetType().Name}: {string.Join(' ', e.Message.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries))}";
    }

    if (bytes >= 0)
    {
        group.Bytes += bytes;
        totalBytes += bytes;
    }
    if (glbMeshes > 0)
    {
        group.Meshes += glbMeshes;
        totalMeshes += glbMeshes;
    }
    if (glbVertices > 0)
    {
        group.Vertices += glbVertices;
        totalVertices += glbVertices;
    }
    group.Files++;
    if (outcome == "ok")
    {
        group.Loaded++;
    }
    group.Mismatch += outcome == "mismatch" ? 1 : 0;

    Console.WriteLine($"file {source} bytes={bytes} cat={catalogMeshes}m/{catalogVertices}v glb={glbMeshes}m/{glbVertices}v {outcome}");
}

foreach (var (key, group) in groups)
{
    var empty = group.Files == 0;
    var meshless = requireMeshes && group.Meshes == 0;
    if (empty || meshless)
    {
        failures++;
    }
    Console.WriteLine($"group {key} files={group.Files} loaded={group.Loaded} bytes={group.Bytes} "
        + $"meshes={group.Meshes} vertices={group.Vertices} mismatches={group.Mismatch}"
        + (empty ? " EMPTY" : "") + (meshless ? " NO_MESHES" : ""));
}

Console.WriteLine($"summary files={files} loaded={loaded} failed={failed} mismatches={mismatches} "
    + $"bytes={totalBytes} meshes={totalMeshes} vertices={totalVertices} groups={groups.Count} failures={failures}");
return failures == 0 ? 0 : 1;

internal sealed class Group
{
    public int Files;
    public int Loaded;
    public int Mismatch;
    public long Bytes;
    public long Meshes;
    public long Vertices;
}
EOF

echo "m0-acceptance: building the checker -> $checker_dir"
dotnet build "$checker_dir/glbcheck.csproj" -c Release -v q --nologo

# --- Step 1: pre-flight on the 10 pinned characters --------------------------

under_args=()
for char_dir in "${chars[@]}"; do
  under_args+=(--under "$char_dir")
done

stamp "pre-flight: 10 characters -> $out_lib"
"${cli[@]}" convert \
  --root "$SOTC_GAME_ROOT" \
  --out "$out_lib" \
  "${under_args[@]}" \
  --ext cmsh \
  --jobs "$jobs"

check_args=()
for char_dir in "${chars[@]}"; do
  check_args+=(--prefix "$char_dir")
done

stamp "pre-flight: SharpGLTF load + meshes>=1 + catalog cross-check"
dotnet exec "$checker_dll" --lib "$out_lib" --require-meshes "${check_args[@]}"

# --- Step 2: largest-model check (Review Focus #4) ---------------------------

largest_line="$(find "$SOTC_GAME_ROOT/$statue_dir" -iname '*.cmsh' -exec wc -c {} + \
  | awk '$2 != "total" {print $1, $2}' | sort -rn | head -1)"
largest_path="$(printf '%s' "$largest_line" | awk '{print $2}')"
largest_rel="${largest_path#"$SOTC_GAME_ROOT"/}"

stamp "largest-model check: $largest_line"
"${cli[@]}" convert \
  --root "$SOTC_GAME_ROOT" \
  --out "$out_lib" \
  --under "$statue_dir" \
  --ext cmsh \
  --jobs "$jobs"

# Two explicit checks: the pinned directory, then the pinned largest source itself (a prefix owns
# the source, so nested prefixes would leave the file's group empty — keep them separate).
dotnet exec "$checker_dll" --lib "$out_lib" --require-meshes --prefix "$statue_dir"
dotnet exec "$checker_dll" --lib "$out_lib" --require-meshes --prefix "$largest_rel"

# --- Step 3: full batch — launch, or prove convergence of a finished one -----

if [[ -f "$log" ]] && grep -q '^in scope:' "$log"; then
  # A previous full-batch invocation finished (its summary is in the log): converge, don't relaunch.
  stamp "convergence: re-invoking the full batch (expect converted: 0)"
  "${cli[@]}" convert \
    --root "$SOTC_GAME_ROOT" \
    --out "$out_lib" \
    --under models --under characters \
    --ext cmsh \
    --jobs "$jobs" > "$conv_log" 2>&1 || { tail -5 "$conv_log" >&2; exit 1; }
  cat "$conv_log" >> "$log"
  if ! grep -qE 'converted: 0([[:space:]]+failed:)' "$conv_log"; then
    echo "m0-acceptance: CONVERGENCE FAILED — the second full invocation converted sources:" >&2
    tail -3 "$conv_log" >&2
    exit 1
  fi
  stamp "convergence: ok — second invocation converted 0 sources"
  tail -1 "$conv_log"
else
  lib_kb="$(du -sk "$out_lib" | awk '{print $1}')"
  free_kb="$(df -k "$out_lib" | awk 'NR==2 {print $4}')"
  if [[ "$lib_kb" -gt "$library_budget_kb" ]]; then
    echo "m0-acceptance: $out_lib is already $((lib_kb / 1024 / 1024)) GB (budget 35 GB); refusing to launch" >&2
    exit 1
  fi
  if [[ "$free_kb" -lt "$volume_floor_kb" ]]; then
    echo "m0-acceptance: only $((free_kb / 1024 / 1024)) GB free; refusing to launch the batch" >&2
    exit 1
  fi

  stamp "batch: launching in the background (log: $log)"
  nohup "${cli[@]}" convert \
    --root "$SOTC_GAME_ROOT" \
    --out "$out_lib" \
    --under models --under characters \
    --ext cmsh \
    --jobs "$jobs" > "$log" 2>&1 &
  echo $! > "$pid_file"
  stamp "batch: pid $(cat "$pid_file") — monitor with: wc -l $log"
fi

# --- Summary ------------------------------------------------------------------

"${cli[@]}" report --out "$out_lib" --top 20
