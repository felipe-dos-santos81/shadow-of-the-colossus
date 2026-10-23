# SOTC Native — Apple-Silicon rebuild of Shadow of the Colossus (2018).
# `make help` lists the targets. Paths are overridable: make run GAME_ROOT=... LIB=...

GAME_ROOT ?= /Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game
UROOT     ?= /Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/uroot
LIB       ?= $(CURDIR)/out/library
BIN       ?= $(CURDIR)/out/bin
JOBS      ?= 8
f         ?= models/shadow/characters/wander/_cmn/wander.cmsh

export SOTC_GAME_ROOT := $(GAME_ROOT)
export SOTC_LIB       := $(LIB)
export SOTC_UROOT     := $(UROOT)

CLI       = dotnet exec $(BIN)/bp-convert.dll
# The machine suspends idle work: every viewer and acceptance run is kept awake.
AWAKE     = caffeinate -dimsu
VIEWER    = $(AWAKE) app/build/sotc-viewer --library "$(LIB)" \
            --model models/shadow/characters/wander/_cmn/wander.glb
ENCOUNTER = out/re/stage/encounter.manifest
SHRINE    = out/re/stage/shrine.manifest

.PHONY: help build publish test convert audio inspect report app app-test \
        run shrine viewer shrine-manifest probe-check accept clean

help: ## List the targets
	@grep -E '^[a-z0-9%-]+:.*## ' $(MAKEFILE_LIST) | awk -F ':.*## ' '{printf "  \033[36m%-16s\033[0m %s\n", $$1, $$2}'

# ── Converter (.NET) ─────────────────────────────────────────────────────────

build: ## Build the converter solution
	dotnet build BpConvert.sln

publish: ## Publish bp-convert to out/bin
	dotnet publish src/BpConvert.Cli -c Release -o $(BIN)

test: ## Run the converter tests
	dotnet test tests/BpConvert.Tests -v q

convert: publish ## Convert the cmsh batch into the library (resumable; force=1, JOBS=N; ~33 min full)
	$(CLI) convert --root "$(GAME_ROOT)" --out "$(LIB)" --under models --under characters \
		--ext cmsh --jobs $(JOBS) $(if $(force),--force)

audio: publish ## Decode the curated audio slice (the S6 map, 10 cues)
	$(AWAKE) $(CLI) audio --root "$(GAME_ROOT)" --out "$(LIB)" --map tools/re/audio/s6_map.json --force

inspect: publish ## Inspect one source container (f=models/.../wander.cmsh)
	$(CLI) inspect "$(GAME_ROOT)/$(f)"

report: publish ## Summarize the library catalog (top=N)
	$(CLI) report --out "$(LIB)" $(if $(top),--top $(top))

# ── Viewer (C++/SDL3) ────────────────────────────────────────────────────────

app: ## Configure and build the viewer (Release)
	cmake -S app -B app/build -DCMAKE_BUILD_TYPE=Release && cmake --build app/build -j

app-test: app ## Run the viewer's unit and windowed tests
	ctest --test-dir app/build --output-on-failure

run: app ## Play the Valus encounter (defeat him to return to the shrine)
	@test -f $(ENCOUNTER) || { echo "missing $(ENCOUNTER): run make accept-s4"; exit 1; }
	$(VIEWER) --manifest $(ENCOUNTER)

shrine: app shrine-manifest ## Start in the Shrine of Worship
	$(VIEWER) --manifest $(ENCOUNTER) --stage shrine

viewer: app ## Show Wander alone (no stage)
	$(VIEWER)

shrine-manifest: ## Compose out/re/stage/shrine.manifest from the stage data
	@test -f $(SHRINE) || python3 tools/re/stage/probe.py compose --stage shrine --library "$(LIB)" --out $(SHRINE)

probe-check: ## Run the RE probes' pinned gates (anb, cwks, arena + shrine manifests)
	python3 tools/re/anb/probe.py check-skb
	python3 tools/re/stage/probe.py check-cwks
	python3 tools/re/stage/probe.py check-manifest --library "$(LIB)"
	python3 tools/re/stage/probe.py check-encounter --library "$(LIB)"
	python3 tools/re/stage/probe.py check-manifest --stage shrine --library "$(LIB)" $(SHRINE)

# ── Acceptance ───────────────────────────────────────────────────────────────

accept: accept-s7 ## Run the current milestone's acceptance (S7 → S6 → … → S3)

accept-%: ## Run one acceptance script: accept-m0, -re0, -s1, -s3 … -s7 (one at a time)
	$(AWAKE) ./scripts/$*-acceptance.sh

clean: ## Remove build outputs (out/bin, app/build); the library and out/re stay
	rm -rf $(BIN) app/build
