# Makefile for SOTC Native — Apple-Silicon rebuild of Shadow of the Colossus (2018)
# Targets are numbered by pipeline stage:
#   env → 0 build → 1 convert → 2 inspect → 3 app → 4 re → accept → dev
SERVICE = SOTC Native

# Variables
REPO_ROOT := $(CURDIR)
GAME_ROOT ?= /Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game
UROOT ?= /Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/uroot
LIBRARY ?= $(REPO_ROOT)/out/library
BIN_DIR ?= $(REPO_ROOT)/out/bin
CLI = dotnet exec $(BIN_DIR)/bp-convert.dll
PYTHON = python3
JOBS ?= 8
VIEWER = $(REPO_ROOT)/app/build/sotc-viewer
WANDER = models/shadow/characters/wander/_cmn/wander.glb
f ?= models/shadow/characters/wander/_cmn/wander.cmsh

.PHONY: help build publish convert inspect report \
        app app-test app-run \
        re-anb-check \
        test accept accept-m0 accept-re0 accept-s1 \
        clean

# ── Environment ──────────────────────────────────────────────────────────────

help: ## Print this help message
	@printf '\033[01;32m${SERVICE} — Apple-Silicon rebuild pipeline\033[00;37m\n\n'
	@printf "\033[33mUsage:\033[0m\n  make [target] [arg=\"val\"...]\n\n\033[33mTargets:\033[0m\n"
	@grep -E '^[-a-zA-Z0-9_\.\/]+:.*?## .*$$' $(MAKEFILE_LIST) | \
		awk 'BEGIN {FS = ":.*?## "}; \
		{printf "  \033[36m%-26s\033[0m %s\n", $$1, $$2}'

# ── Stage 0 · Build (.NET toolchain) ─────────────────────────────────────────

build: ## [STEP 0] Build the converter toolchain (dotnet build BpConvert.sln)
	dotnet build BpConvert.sln

publish: ## [STEP 0] Publish bp-convert to out/bin (the CLI the targets below use)
	dotnet publish src/BpConvert.Cli -c Release -o $(BIN_DIR)

# ── Stage 1 · Convert (game tree → asset library) ────────────────────────────

# Resumable: sources recorded ok in the catalog are skipped; force=1 reconverts.
# A full rerun writes ~12 GB / takes ~33 min at jobs=8 — run it in the background.
convert: publish ## [STEP 1] Convert game assets into the library (resumable; force=1; jobs=N)
	SOTC_GAME_ROOT=$(GAME_ROOT) $(CLI) convert \
		--root "$(GAME_ROOT)" --out "$(LIBRARY)" \
		--under models --under characters --ext cmsh \
		--jobs $(JOBS) $(if $(force),--force)

# ── Stage 2 · Inspect / report (source containers + catalog) ─────────────────

inspect: publish ## [STEP 2] Inspect one source container (f=models/.../wander.cmsh)
	SOTC_GAME_ROOT=$(GAME_ROOT) $(CLI) inspect "$(GAME_ROOT)/$(f)"

report: publish ## [STEP 2] Report the library catalog (top=N limits rows)
	SOTC_GAME_ROOT=$(GAME_ROOT) $(CLI) report --out "$(LIBRARY)" $(if $(top),--top $(top))

# ── Stage 3 · App (SDL viewer) ───────────────────────────────────────────────

app: ## [STEP 3] Configure and build the SDL app (app/build)
	cmake -S app -B app/build && cmake --build app/build -j

app-test: app ## [STEP 3] Run the app test suite (ctest; needs SOTC_LIB)
	SOTC_LIB=$(LIBRARY) ctest --test-dir app/build --output-on-failure

app-run: app ## [STEP 3] Run the windowed viewer (Wander + procedural clip; cursor never captured)
	SOTC_LIB=$(LIBRARY) $(VIEWER) --library "$(LIBRARY)" --model "$(WANDER)"

# ── Stage 4 · RE (probes) ────────────────────────────────────────────────────

re-anb-check: ## [STEP 4] Run the RE-1 probe's pinned self-checks (anb container + skeleton bank)
	SOTC_GAME_ROOT=$(GAME_ROOT) $(PYTHON) tools/re/anb/probe.py check-skb

# ── Acceptance ───────────────────────────────────────────────────────────────

test: ## Run the converter unit tests (needs SOTC_GAME_ROOT; 56/57 pre-existing failure)
	SOTC_GAME_ROOT=$(GAME_ROOT) dotnet test tests/BpConvert.Tests -v q

accept: accept-m0 accept-s1 ## Run the converter + viewer acceptance passes (re0 is separate: ~8 min)

accept-m0: ## Run the M0 converter acceptance pass (pinned 10 characters + largest model)
	SOTC_GAME_ROOT=$(GAME_ROOT) ./scripts/m0-acceptance.sh

accept-re0: ## Run the RE-0 acceptance pass (unwrap + Ghidra baseline; ~8 min; needs SOTC_UROOT)
	SOTC_UROOT=$(UROOT) ./scripts/re0-acceptance.sh

accept-s1: ## Run the S1 viewer acceptance pass (build, ctest, selftest, screenshots)
	SOTC_LIB=$(LIBRARY) ./scripts/s1-acceptance.sh

# ── Development ──────────────────────────────────────────────────────────────

clean: ## Remove build outputs (out/bin, app/build); the library and out/re are kept
	rm -rf $(BIN_DIR) app/build
	@echo "Cleanup complete (out/library and out/re kept)."
