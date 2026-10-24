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
ROAD      = out/re/stage/road.manifest
ARENA02   = out/re/stage/arena_02.manifest
ARENA03   = out/re/stage/arena_03.manifest
# The clips `make bake` puts into wander.glb, in order: the 15 S2–S6 clips, the
# two S7 wake cuts (docs/notes/s7-recon.md §6), the 26 S8 ride clips
# (docs/notes/s8-recon.md), then the six S9 raise clips at 43…48 (design D9).
# The order is the clip index.
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
               player_a_whistle player_a_whistle_near \
               player_a_hoist_sword_start player_a_hoist_sword_loop player_a_hoist_sword_end \
               player_a_uma_hoist_sword_start player_a_uma_hoist_sword_loop \
               player_a_uma_hoist_sword_end
# The sword's light atlases (tools/re/s9/placements.json textures.sources: lines, overlay,
# flare, hit — hit is the flare's atlas, so the last line rewrites the same PNG).
SWORD_TEXTURES = vfx/common/beam/_ps4/vertical_solid_beams_01_pfx \
                 vfx/common/beam/_ps4/vertical_beams_wave_01_pfx \
                 vfx/common/glows/_ps4/soft_glow_01_falloff_pfx \
                 vfx/common/glows/_ps4/soft_glow_01_falloff_pfx
# Agro's clips: the S4.2 kick first (index 0, pinned by name), then the 19 S8 riding clips.
AGRO_CLIPS = horse_a_add_horse_kick horse_a_uma_stand1 horse_a_uma_walk_start \
             horse_a_uma_walk horse_a_uma_walk_stop horse_a_uma_run_n_start \
             horse_a_uma_run_n horse_a_uma_run_n_stop horse_a_uma_run_n_from_walk \
             horse_a_uma_run_start horse_a_uma_run horse_a_uma_run_from_run_n \
             horse_a_uma_run_stop1 horse_a_uma_run_stop2 horse_a_uma_run_stop3 \
             horse_a_uma_back_start horse_a_uma_back horse_a_uma_back_stop \
             horse_a_uma_neigh horse_a_uma_neigh_end
# Quadratus's 58 clips (docs/notes/colossi-census.md, S11 recon), every cp3shadow/anim/boss_a_*.anb,
# in byte order (LC_ALL=C sort). The order is the clip index. Baked only by `make bake-quadratus`.
QUADRATUS_CLIPS = boss_a_add_bl_shoulder_top_damage boss_a_add_body_damage \
                  boss_a_add_br_shoulder_top_damage boss_a_add_fl_foot_damage \
                  boss_a_add_fl_shoulder_top_damage boss_a_add_fr_foot_damage \
                  boss_a_add_fr_shoulder_top_damage boss_a_add_hara_r_damage \
                  boss_a_add_head_damage boss_a_add_insight_dummy boss_a_add_neck_l_damage \
                  boss_a_add_neck_r_damage boss_a_angry_1 boss_a_attack_l boss_a_attack_l_end \
                  boss_a_attack_l_loop boss_a_attack_r boss_a_attack_r_cancel \
                  boss_a_attack_r_end boss_a_attack_r_loop boss_a_attack_r_start boss_a_back \
                  boss_a_boot boss_a_boot_cancel boss_a_boot_walk_start boss_a_death \
                  boss_a_death_loop boss_a_down_b_l_end boss_a_down_b_l_loop \
                  boss_a_down_b_l_start boss_a_down_b_r_end boss_a_down_b_r_loop \
                  boss_a_down_b_r_start boss_a_down_f_l_end boss_a_down_f_l_loop \
                  boss_a_down_f_l_start boss_a_down_f_r_end boss_a_down_f_r_loop \
                  boss_a_down_f_r_start boss_a_limit_pose boss_a_refuse_arm_l \
                  boss_a_refuse_arm_r boss_a_refuse_head boss_a_refuse_leg_l \
                  boss_a_refuse_leg_r boss_a_refuse_shoulder boss_a_return_pos \
                  boss_a_scale_center boss_a_search boss_a_stand boss_a_standby boss_a_turn_l \
                  boss_a_turn_r boss_a_walk boss_a_walk_end boss_a_walk_start \
                  boss_a_walk_turn_l boss_a_walk_turn_r
# Gaius's 62 clips (S12 design D11; recon arena03/), every cp3shadow/anim/knight_a_*.anb, in byte
# order (LC_ALL=C sort). The order is the clip index. Baked by `make bake-knight`, part of `bake` (ruling R-C).
KNIGHT_CLIPS = knight_a_add_arm_l_back_damage knight_a_add_arm_l_front_damage knight_a_add_arm_l_up_end \
               knight_a_add_arm_l_up_loop knight_a_add_arm_l_up_start knight_a_add_arm_r_front_damage \
               knight_a_add_arm_r_up_end knight_a_add_arm_r_up_loop knight_a_add_arm_r_up_start \
               knight_a_add_body_back_damage knight_a_add_body_back_damage_weak knight_a_add_face_damage \
               knight_a_add_hara_back_damage knight_a_add_hara_front_damage knight_a_add_head_damage \
               knight_a_add_insight_dummy knight_a_add_jowan_l_back_damage knight_a_add_jowan_l_front_damage \
               knight_a_add_jowan_r_back_damage knight_a_add_jowan_r_front_damage knight_a_add_mune_damage \
               knight_a_add_neck_back_damage knight_a_add_shoulder_l_damage knight_a_add_shoulder_r_damage \
               knight_a_atk1 knight_a_atk1_2 knight_a_atk1_3 knight_a_atk1_end knight_a_atk1_fail \
               knight_a_atk1_fail2 knight_a_atk1_start knight_a_atk2_end knight_a_atk2_loop \
               knight_a_atk2_start knight_a_atk_leg_l knight_a_atk_leg_l_fail knight_a_atk_leg_l_start \
               knight_a_atk_leg_r knight_a_atk_leg_r_fail knight_a_atk_leg_r_start knight_a_back knight_a_boot \
               knight_a_boot_cancel knight_a_death_loop knight_a_knight_death knight_a_limit_pose \
               knight_a_refuse_arm_l knight_a_refuse_arm_r knight_a_refuse_body knight_a_refuse_head \
               knight_a_scale_center knight_a_search knight_a_slide_l knight_a_slide_r knight_a_stand \
               knight_a_standby knight_a_turn_back knight_a_turn_l knight_a_turn_r knight_a_walk \
               knight_a_walk_end knight_a_walk_start
# Phaedra's 75 body clips (S13 recon, kirin/findings.md), every cp3shadow/anim/kirin_a_*.anb except the
# two ladder rest poses (hashigo_{l,r}_base: separate skeletons), in byte order (LC_ALL=C sort). The order is
# the clip index. Baked by `make bake-kirin`, part of `bake`.
KIRIN_CLIPS = kirin_a_add_angry kirin_a_add_dmg_head_min kirin_a_add_head_refuse \
              kirin_a_add_hip_damage kirin_a_add_insight_dummy kirin_a_add_mune_back_l_damage \
              kirin_a_add_mune_back_r_damage kirin_a_add_mune_front_damage \
              kirin_a_add_mune_ke_l_damage kirin_a_add_mune_ke_r_damage \
              kirin_a_add_neck_down_1_end kirin_a_add_neck_down_1_loop \
              kirin_a_add_neck_down_1_start kirin_a_add_neck_down_1b_end \
              kirin_a_add_neck_down_1b_loop kirin_a_add_neck_down_1b_start \
              kirin_a_add_neck_down_2_end kirin_a_add_neck_down_2_loop \
              kirin_a_add_neck_down_2_loop_low kirin_a_add_neck_down_2_refuse \
              kirin_a_add_neck_down_2_start kirin_a_add_neck_down_2_start2 \
              kirin_a_add_neck_refuse_end kirin_a_add_neck_refuse_loop \
              kirin_a_add_neck_refuse_start kirin_a_add_shoulder_l_damage \
              kirin_a_add_shoulder_r_damage kirin_a_add_tail_l_damage_ kirin_a_add_tail_r_damage_ \
              kirin_a_atack_leg_b_l kirin_a_atack_leg_b_r kirin_a_atack_leg_l_a \
              kirin_a_atack_leg_l_a_wait kirin_a_atack_leg_l_b kirin_a_atack_leg_l_b_wait \
              kirin_a_atack_leg_l_c kirin_a_atack_leg_l_c_wait kirin_a_atack_leg_l_d \
              kirin_a_atack_leg_r_a kirin_a_atack_leg_r_a_wait kirin_a_atack_leg_r_b \
              kirin_a_atack_leg_r_b_wait kirin_a_atack_leg_r_c kirin_a_atack_leg_r_c_wait \
              kirin_a_atack_leg_r_d kirin_a_boot kirin_a_boot_cancel kirin_a_death \
              kirin_a_death_loop kirin_a_limit_pose kirin_a_refuse_body kirin_a_refuse_mune \
              kirin_a_refuse_tail kirin_a_scale_center kirin_a_seach kirin_a_stand kirin_a_standby \
              kirin_a_threat kirin_a_turn_l kirin_a_turn_r kirin_a_walk kirin_a_walk_angry_end \
              kirin_a_walk_angry_loop kirin_a_walk_angry_start kirin_a_walk_back kirin_a_walk_end \
              kirin_a_walk_start kirin_a_walk_turn_l kirin_a_walk_turn_r kirin_a_watch2_end \
              kirin_a_watch2_loop kirin_a_watch2_start kirin_a_watch_end kirin_a_watch_loop \
              kirin_a_watch_start
comma := ,
space := $(subst ,, )

.PHONY: help build publish test convert audio inspect report app app-test \
        bake bake-quadratus bake-knight run shrine plain road quadratus gaius viewer shrine-manifest plain-manifest road-manifest arena02-manifest arena03-manifest probe-check accept clean libview \
        verify verify-long behaviour \
        routes terrain coverage bake-kirin arena04-manifest phaedra accept-s13

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

audio: publish ## Decode the curated audio slice (the S9 map, 36 entries)
	$(AWAKE) $(CLI) audio --root "$(GAME_ROOT)" --out "$(LIB)" --map tools/re/audio/s9_map.json --force

bake: behaviour publish bake-quadratus bake-knight bake-kirin routes terrain ## Bake Mono's pose, Wander's 49 clips, Agro's 20, the sword's atlases, Quadratus's 58, Gaius's 62 + his graph, Phaedra's 75 + her routes and terrain (rewrites the glbs)
	$(AWAKE) $(CLI) convert --root "$(GAME_ROOT)" --out "$(LIB)" \
		--under models/shadow/characters/mono/_cmn/mono.cmsh --clips girl_a_death_loop --force
	$(AWAKE) $(CLI) convert --root "$(GAME_ROOT)" --out "$(LIB)" \
		--under models/shadow/characters/wander/_cmn/wander.cmsh --force --clips $(subst $(space),$(comma),$(strip $(WANDER_CLIPS)))
	$(AWAKE) $(CLI) convert --root "$(GAME_ROOT)" --out "$(LIB)" \
		--under models/shadow/characters/agro/_cmn/agro.cmsh --force --clips $(subst $(space),$(comma),$(strip $(AGRO_CLIPS)))
	$(foreach t,$(SWORD_TEXTURES),$(AWAKE) $(CLI) texture --in "$(GAME_ROOT)/textures/$(t).cptx" \
		--out "$(LIB)/textures/$(t).png" &&) true

# S11: part of `bake` since the promotion to the base library (ruling A16).
bake-quadratus: publish ## Bake Quadratus (colossus_02_bull_a) with his 58 clips (rewrites his glb)
	$(AWAKE) $(CLI) convert --root "$(GAME_ROOT)" --out "$(LIB)" \
		--under models/shadow/characters/colossus_02_bull_a/_cmn/colossus_02_bull_a.cmsh --force --clips $(subst $(space),$(comma),$(strip $(QUADRATUS_CLIPS)))

# S12: part of `bake` (D11, ruling R-C); the branch baked into its view, the merge promotes it to the base.
bake-knight: publish ## Bake Gaius (colossus_03_knight) with his 62 clips + the events sidecar (rewrites his glb)
	$(AWAKE) $(CLI) convert --root "$(GAME_ROOT)" --out "$(LIB)" \
		--under models/shadow/characters/colossus_03_knight/_cmn/colossus_03_knight.cmsh --force --clips $(subst $(space),$(comma),$(strip $(KNIGHT_CLIPS)))

inspect: publish ## Inspect one source container (f=models/.../wander.cmsh)
	$(CLI) inspect "$(GAME_ROOT)/$(f)"

report: publish ## Summarize the library catalog (top=N)
	$(CLI) report --out "$(LIB)" $(if $(top),--top $(top))

# ── Viewer (C++/SDL3) ────────────────────────────────────────────────────────

app: ## Configure and build the viewer (Release)
	cmake -S app -B app/build -DCMAKE_BUILD_TYPE=Release && cmake --build app/build -j

app-test: app ## Run the viewer's unit and windowed tests
	ctest --test-dir app/build --output-on-failure

run: app shrine-manifest plain-manifest road-manifest ## Play the Valus encounter (defeat him to return to the shrine)
	@test -f $(ENCOUNTER) || { echo "missing $(ENCOUNTER): run make accept-s4"; exit 1; }
	$(VIEWER) --manifest $(ENCOUNTER)

shrine: app shrine-manifest plain-manifest road-manifest ## Start in the Shrine of Worship (its front door leads to the plain)
	$(VIEWER) --manifest $(ENCOUNTER) --stage shrine

plain: app shrine-manifest plain-manifest road-manifest ## Start on the plain with Agro (Q whistles, E mounts)
	$(VIEWER) --manifest $(ENCOUNTER) --stage plain

road: plain ## The same start as `plain`: interactive play loads the road (C raises the sword)

quadratus: app arena02-manifest ## Fight Quadratus in arena_02 (stab a rear hoof; R resets)
	$(VIEWER) --manifest $(ARENA02)

gaius: app arena03-manifest ## Fight Gaius in arena_03 (dodge his slam, grab the stuck blade; R resets)
	$(VIEWER) --manifest $(ARENA03)

viewer: app ## Show Wander alone (no stage)
	$(VIEWER)

shrine-manifest: ## Compose out/re/stage/shrine.manifest from the stage data
	@test -f $(SHRINE) || python3 tools/re/stage/probe.py compose --stage shrine --library "$(LIB)" --out $(SHRINE)

plain-manifest: ## Compose out/re/stage/plain.manifest from the stage data
	@test -f $(PLAIN) || python3 tools/re/stage/probe.py compose --stage plain --library "$(LIB)" --out $(PLAIN)

road-manifest: ## Compose out/re/stage/road.manifest from the stage data
	@test -f $(ROAD) || python3 tools/re/stage/probe.py compose --stage road --library "$(LIB)" --out $(ROAD)

arena02-manifest: ## Compose out/re/stage/arena_02.manifest (Quadratus's arena) from the stage data
	@test -f $(ARENA02) || python3 tools/re/stage/probe.py compose --stage arena_02 --library "$(LIB)" --out $(ARENA02)

arena03-manifest: ## Compose out/re/stage/arena_03.manifest (Gaius's lake arena) from the stage data
	@test -f $(ARENA03) || python3 tools/re/stage/probe.py compose --stage arena_03 --library "$(LIB)" --out $(ARENA03)

probe-check: ## Run the RE probes' pinned gates (anb, cwks, arena + shrine + plain + road + arena_02 + arena_03 manifests)
	python3 tools/re/anb/probe.py check-skb
	python3 tools/re/stage/probe.py check-cwks
	python3 tools/re/stage/probe.py check-manifest --library "$(LIB)"
	python3 tools/re/stage/probe.py check-encounter --library "$(LIB)"
	python3 tools/re/stage/probe.py check-manifest --stage shrine --library "$(LIB)" $(SHRINE)
	python3 tools/re/stage/probe.py check-manifest --stage plain --library "$(LIB)" $(PLAIN)
	python3 tools/re/stage/probe.py check-manifest --stage road --library "$(LIB)" $(ROAD)
	python3 tools/re/stage/probe.py check-manifest --stage arena_02 --library "$(LIB)" $(ARENA02)
	python3 tools/re/stage/probe.py check-manifest --stage arena_03 --library "$(LIB)" $(ARENA03)

census: ## Census the sixteen colossi into tools/re/census/census.json (read-only; ~2 s)
	python3 -I tools/re/census/census.py --game "$(GAME_ROOT)" --lib "$(LIB)" --check --out tools/re/census/census.json
.PHONY: census

# S12: the colossus mode graphs the app interprets (part of `bake`, ruling R-C).
behaviour: ## Export Gaius's mode graph from out/re/eboot.elf to LIB/behaviour/KNIGHT.graph.json (read-only; ~1 s)
	@test -f out/re/eboot.elf || { echo "out/re/eboot.elf is missing: run scripts/re0-acceptance.sh first" >&2; exit 1; }
	python3 -I tools/re/scr/graph_export.py export KNIGHT "$(LIB)/behaviour/KNIGHT.graph.json"

# S13 (Phaedra): each target fails until the task that fills it lands (Task 1 contracts).
routes: ## Export the .path route graphs to LIB/behaviour/<stem>.route.json (S13 Task 7)
	@echo "not yet (S13 Task 7)" >&2; exit 1

terrain: ## Export arena_04's terrain classes to LIB/stage/arena_04.terrain.json (S13 Task 9)
	@echo "not yet (S13 Task 9)" >&2; exit 1

coverage: ## Per-boss interpreter coverage into out/re/coverage.md (S13 Task 3)
	@echo "not yet (S13 Task 3)" >&2; exit 1

bake-kirin: publish ## Bake Phaedra (colossus_04_kirin) with her 75 clips + the events sidecar (rewrites her glb)
	$(AWAKE) $(CLI) convert --root "$(GAME_ROOT)" --out "$(LIB)" \
		--under models/shadow/characters/colossus_04_kirin/_cmn/c04_kirin.cmsh --force --clips $(subst $(space),$(comma),$(strip $(KIRIN_CLIPS)))

arena04-manifest: ## Compose out/re/stage/arena_04.manifest (Phaedra's arena; the ruins as floors-only lines)
	@test -f out/re/stage/arena_04.manifest || python3 tools/re/stage/probe.py compose --stage arena_04 --library "$(LIB)" --out out/re/stage/arena_04.manifest

phaedra: ## Fight Phaedra on arena_04 (S13 Task 15)
	@echo "not yet (S13 Task 15)" >&2; exit 1

accept-s13: ## Run the S13 acceptance (S13 Task 17)
	@echo "not yet (S13 Task 17)" >&2; exit 1

# ── Acceptance ───────────────────────────────────────────────────────────────

accept: shrine-manifest plain-manifest road-manifest arena02-manifest arena03-manifest accept-s12 ## Run the current milestone's acceptance (S12 → S11 → S9 → S8 → … → S3)

accept-%: ## Run one acceptance script: accept-m0, -re0, -s1, -s3 … -s9, -s11, -s12 (one at a time)
	$(AWAKE) ./scripts/$*-acceptance.sh

verify: shrine-manifest plain-manifest road-manifest arena02-manifest arena03-manifest ## The regression runner: every frozen assertion, re-run when its inputs changed (ARGS="--quick", "--only s7", "--dry-run")
	./scripts/verify $(ARGS)

verify-long: shrine-manifest plain-manifest road-manifest arena02-manifest arena03-manifest ## The milestone gate through the runner: the highest scripts/sN-acceptance.sh
	./scripts/verify --long

libview: ## Clone the library into out/libviews/<BRANCH>/ (APFS, free); prints the SOTC_LIB to export
	@./tools/verify/libview.sh "$(BRANCH)"

clean: ## Remove build outputs (out/bin, app/build); the library and out/re stay
	rm -rf $(BIN) app/build
