# SOTC Native — Apple-Silicon rebuild of Shadow of the Colossus (2018).
# `make help` lists the targets. Paths are overridable: make run GAME_ROOT=... LIB=...

GAME_ROOT ?= /Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/game
UROOT     ?= /Users/felipe.dos.santos/Downloads/sotc/SOTC-extract/extracted/uroot
LIB       ?= $(or $(SOTC_LIB),$(CURDIR)/out/library)
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
PLAIN     = out/re/stage/plain.manifest
# The clips `make bake` puts into wander.glb, in order: the 15 S2–S6 clips, the
# two S7 wake cuts (docs/notes/s7-recon.md §6), then the 26 S8 ride clips
# (docs/notes/s8-recon.md). The order is the clip index.
WANDER_CLIPS = player_a_stand player_a_walk player_a_run player_a_grip_stand_crouch \
               player_a_grip_loop player_a_grip_move_f1 player_a_grip_move_f2 \
               player_a_grip_float_u_hi_attack player_a_atk1 player_a_damage_m_fly2 \
               player_a_fall_damage_to_death player_a_damage_h_fly1 player_a_damage_h_fly_end \
               player_a_fall_damage_start player_a_fall_damage_end \
               player_a_home_boss01_cut04 player_a_home_boss01_cut05 \
               player_a_uma_stand1_player player_a_uma_walk_start player_a_uma_walk \
               player_a_uma_walk_stop player_a_uma_run_n_start player_a_uma_run_n \
               player_a_uma_run_n_stop player_a_uma_run_n_from_walk \
               player_a_uma_run_start player_a_uma_run player_a_uma_run_from_run_n \
               player_a_uma_run_stop1 player_a_uma_run_stop2 player_a_uma_run_stop3 \
               player_a_uma_back_start player_a_uma_back player_a_uma_back_stop \
               player_a_uma_neigh player_a_uma_neigh_end player_a_uma_stand_ride_l \
               player_a_uma_stand_ride_r player_a_uma_stand_getoff_l_start \
               player_a_uma_stand_getoff_r_start player_a_uma_stand_getoff_fall \
               player_a_whistle player_a_whistle_near
# Agro's clips: the S4.2 kick first (index 0, pinned by name), then the 19 S8 riding clips.
AGRO_CLIPS = horse_a_add_horse_kick horse_a_uma_stand1 horse_a_uma_walk_start \
             horse_a_uma_walk horse_a_uma_walk_stop horse_a_uma_run_n_start \
             horse_a_uma_run_n horse_a_uma_run_n_stop horse_a_uma_run_n_from_walk \
             horse_a_uma_run_start horse_a_uma_run horse_a_uma_run_from_run_n \
             horse_a_uma_run_stop1 horse_a_uma_run_stop2 horse_a_uma_run_stop3 \
             horse_a_uma_back_start horse_a_uma_back horse_a_uma_back_stop \
             horse_a_uma_neigh horse_a_uma_neigh_end
comma := ,
space := $(subst ,, )

.PHONY: help build publish test convert audio inspect report app app-test \
        bake run shrine plain viewer shrine-manifest plain-manifest probe-check accept clean libview \
        verify verify-long

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

audio: publish ## Decode the curated audio slice (the S8 map, 34 entries)
	$(AWAKE) $(CLI) audio --root "$(GAME_ROOT)" --out "$(LIB)" --map tools/re/audio/s8_map.json --force

bake: publish ## Bake the animation clips: Mono's pose, Wander's 43 clips and Agro's 20 (rewrites the glbs)
	$(AWAKE) $(CLI) convert --root "$(GAME_ROOT)" --out "$(LIB)" \
		--under models/shadow/characters/mono/_cmn/mono.cmsh --clips girl_a_death_loop --force
	$(AWAKE) $(CLI) convert --root "$(GAME_ROOT)" --out "$(LIB)" \
		--under models/shadow/characters/wander/_cmn/wander.cmsh --force --clips $(subst $(space),$(comma),$(strip $(WANDER_CLIPS)))
	$(AWAKE) $(CLI) convert --root "$(GAME_ROOT)" --out "$(LIB)" \
		--under models/shadow/characters/agro/_cmn/agro.cmsh --force --clips $(subst $(space),$(comma),$(strip $(AGRO_CLIPS)))

inspect: publish ## Inspect one source container (f=models/.../wander.cmsh)
	$(CLI) inspect "$(GAME_ROOT)/$(f)"

report: publish ## Summarize the library catalog (top=N)
	$(CLI) report --out "$(LIB)" $(if $(top),--top $(top))

# ── Viewer (C++/SDL3) ────────────────────────────────────────────────────────

app: ## Configure and build the viewer (Release)
	cmake -S app -B app/build -DCMAKE_BUILD_TYPE=Release && cmake --build app/build -j

app-test: app ## Run the viewer's unit and windowed tests
	ctest --test-dir app/build --output-on-failure

run: app shrine-manifest plain-manifest ## Play the Valus encounter (defeat him to return to the shrine)
	@test -f $(ENCOUNTER) || { echo "missing $(ENCOUNTER): run make accept-s4"; exit 1; }
	$(VIEWER) --manifest $(ENCOUNTER)

shrine: app shrine-manifest plain-manifest ## Start in the Shrine of Worship (its front door leads to the plain)
	$(VIEWER) --manifest $(ENCOUNTER) --stage shrine

plain: app shrine-manifest plain-manifest ## Start on the plain with Agro (Q whistles, E mounts)
	$(VIEWER) --manifest $(ENCOUNTER) --stage plain

viewer: app ## Show Wander alone (no stage)
	$(VIEWER)

shrine-manifest: ## Compose out/re/stage/shrine.manifest from the stage data
	@test -f $(SHRINE) || python3 tools/re/stage/probe.py compose --stage shrine --library "$(LIB)" --out $(SHRINE)

plain-manifest: ## Compose out/re/stage/plain.manifest from the stage data
	@test -f $(PLAIN) || python3 tools/re/stage/probe.py compose --stage plain --library "$(LIB)" --out $(PLAIN)

probe-check: ## Run the RE probes' pinned gates (anb, cwks, arena + shrine + plain manifests)
	python3 tools/re/anb/probe.py check-skb
	python3 tools/re/stage/probe.py check-cwks
	python3 tools/re/stage/probe.py check-manifest --library "$(LIB)"
	python3 tools/re/stage/probe.py check-encounter --library "$(LIB)"
	python3 tools/re/stage/probe.py check-manifest --stage shrine --library "$(LIB)" $(SHRINE)
	python3 tools/re/stage/probe.py check-manifest --stage plain --library "$(LIB)" $(PLAIN)

census: ## Census the sixteen colossi into tools/re/census/census.json (read-only; ~2 s)
	python3 -I tools/re/census/census.py --game "$(GAME_ROOT)" --lib "$(LIB)" --check --out tools/re/census/census.json
.PHONY: census

# ── Acceptance ───────────────────────────────────────────────────────────────

accept: shrine-manifest plain-manifest accept-s8 ## Run the current milestone's acceptance (S8 → S7 → S6 → … → S3)

accept-%: ## Run one acceptance script: accept-m0, -re0, -s1, -s3 … -s8 (one at a time)
	$(AWAKE) ./scripts/$*-acceptance.sh

verify: shrine-manifest plain-manifest ## The regression runner: every frozen assertion, re-run when its inputs changed (ARGS="--quick", "--only s7", "--dry-run")
	./scripts/verify $(ARGS)

verify-long: shrine-manifest plain-manifest ## The milestone gate through the runner: the highest scripts/sN-acceptance.sh
	./scripts/verify --long

libview: ## Clone the library into out/libviews/<BRANCH>/ (APFS, free); prints the SOTC_LIB to export
	@./tools/verify/libview.sh "$(BRANCH)"

clean: ## Remove build outputs (out/bin, app/build); the library and out/re stay
	rm -rf $(BIN) app/build
