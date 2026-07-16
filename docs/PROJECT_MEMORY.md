# SkyAnchor Project Memory

This file is the persistent handoff for future conversations. Read it before taking
action, then read the active plan linked below.

## Current context

- Last updated: 2026-07-16
- Active branch: `feature/semifinal-demo-experience`
- Recorded implementation HEAD: `f0e6af0` (`feat: add semifinal cockpit and delivery evidence`).
  This memory update is committed immediately after that implementation checkpoint.
- Worktree: expected clean after the handoff commit; no implementation changes remain
  intentionally uncommitted.
- Branch base: `main@5fc0c28`
- Active plan: [Semifinal Demo Experience Plan](SEMIFINAL_DEMO_EXPERIENCE_PLAN.md)
- Current status: the ordinary P4 cockpit, real completion receipt, and dark task handoff
  page are implemented and committed. The temporary power-on receipt preview is removed.
  The latest P4 implementation has not been compiled; target-hardware performance and
  visual regression remain. The next authorized effort is the Mini Program digital twin.

## Current objective

Prepare the project for the on-site semifinal presentation by turning existing technical
capabilities into a memorable, interactive experience for judges. The emphasis is not a
background stability framework. The emphasis is making real AI, AprilTag, docking,
weather protection, weight verification, photo evidence, and cloud collaboration visible
and understandable.

The presentation theme is:

> SkyAnchor intelligent micro-port: visible decisions, judge-driven safety challenges,
> and verifiable delivery evidence.

## Decisions locked with the user

1. Work in one separate branch: `feature/semifinal-demo-experience`.
2. Highest priority: transparent AI visual docking cockpit on the ESP32-P4 display.
3. Second priority: five-stage trusted delivery evidence chain and completion receipt.
4. Current implementation scope is ordinary mode only. Safety takeover, its UI, its
   10-second countdown, and its fonts are frozen with zero authorized changes.
5. Interactive safety challenges remain deferred and the safety route stays frozen. The
   user authorized the Mini Program digital twin as the next implementation effort on
   2026-07-16.
6. Edge-autonomous delivery during a cloud outage remains optional and deferred.
7. Do not spend this effort on backup packages, generic health dashboards, broad
   refactoring, or invisible monitoring features.
8. Preserve the current business flow, model, thresholds, protocol, and mechanical
   parameters unless a later user decision explicitly changes them.
9. The ordinary task handoff page is part of normal mode only. It remains visible for
   1.5 seconds, accepts Tag 0, and displays no hard-coded communication/weather state.
10. Do not show a delivery report at power-on. The completion receipt is reserved for a
    real, generation-matched ordinary task entering `COMPLETED`.
11. Ordinary voice prompts use volume 75. Safety-specific missing/returned-drone prompts
    remain at volume 65. Do not compile until the user explicitly authorizes it.
12. Implement the digital twin in the Mini Program first. Existing P4 task states are
    sufficient for a truthful high-level first version, so P4 is not changed by default.
13. If the twin must distinguish real door/tray/cargo stages, P4 changes are limited to
    additive, read-only fields in the cloud-state publishing adapter (for example
    `mechanical_stage` and real `weight_g`) plus corresponding cloud pass-through. Do not
    change the P4 display, task state machine, AI/AprilTag decisions, CH32 protocol,
    mechanical actions, thresholds, safety route, or timing for the twin.
14. The twin must never infer a physical stage from elapsed time or let an animation run
    ahead of the last reported device state. Missing or unknown fields render neutrally.

## Existing capabilities to reuse

- `app_drone_ai.cpp` already calculates drone and no-drone softmax scores plus motion
  score, but the public statistics do not yet expose the latest scores.
- `app_vision_result_t` already contains Tag ID, all four Tag corners, center, bounding
  box, edge size, angle, detection time, stable count, and source dimensions.
- The dock judge already produces filtered centering, distance, stability, identity, and
  ready decisions.
- The control UI already receives a flow snapshot and CH32 weight/stage information.
- The weather simulation and safety takeover paths already exist.
- Delivery photo capture already produces a JPEG, SD-card path, upload state, and
  SHA-256 digest.
- The Mini Program already has an order flow, delivery report, photo preview, runtime
  summary, and event timeline that can be upgraded rather than replaced.

## Progress

- [x] Read-only scan of the complete repository and current architecture.
- [x] Current known-good changes committed and pushed on `main` as `5fc0c28`.
- [x] Creative semifinal demonstration direction agreed with the user.
- [x] Created `feature/semifinal-demo-experience`.
- [x] Added the persistent memory mechanism and implementation plan.
- [x] Phase 1 code: compact ordinary P4 cockpit with real AI/dock/Tag data.
- [x] Phase 2 P4 code: generation-isolated trace and ordinary completion receipt.
- [x] Corrected the ordinary HUD bottom margin and retained the last real distance after
  authorization so the permitted state does not regress to `距离 --` when the Tag leaves.
- [x] Added the ordinary 1.5-second dark task handoff page with generation cancellation,
  truthful target data, and correct Tag 0 handling.
- [x] Removed the temporary power-on delivery-report preview and its preview-only API/state.
- [x] Set normal audio prompts to volume 75 while preserving safety prompt volume 65.
- [x] Committed the ordinary cockpit/evidence implementation as `f0e6af0`.
- [ ] Target-hardware FPS, 10-minute heap, glyph rendering, and full safety visual regression.
- [ ] Phase 3: judge safety challenge experience (deferred; safety route frozen).
- [ ] Phase 4: Mini Program digital twin (authorized; active next effort).
- [ ] Optional phase: cloud-outage edge autonomy and delayed synchronization.

## Next action

Start Phase 4 in the Mini Program order-detail page without compiling or changing P4:

1. Define one explicit twin view model from the order status, `last_device_state`, current
   service/device state, weather state, and only other real fields already returned by the
   cloud function. Document neutral fallbacks for missing and older payloads.
2. Implement the lightweight 2D port, drone, door, tray, cargo, cloud link, weather overlay,
   and evidence marker in order-detail WXML/WXSS/JS. Animation is presentation-only and
   changes only when the authoritative state changes; polling must not restart it.
3. Validate active, completed, fault, weather-blocked, offline, missing-field, and repeated-
   polling cases in the WeChat developer tool and on a normal phone.
4. Only if exact mechanical stages cannot be represented truthfully with current payloads,
   add backward-compatible `mechanical_stage`/real-weight fields at the P4 cloud publisher
   and cloud-function pass-through. Keep this isolated from all P4 control and display code.
5. P4 build/flash and the previously listed FPS, heap, ordinary-flow, receipt, and complete
   safety visual/timing acceptance pass remain outstanding. Run them only after the user
   explicitly authorizes compilation.

## Validation state

- An earlier ESP-IDF 5.5.3 build of the cockpit/receipt implementation passed and produced
  `build/Embedded_competition.bin` (`0x4d3a70`, 31% app partition free). That binary is
  stale relative to the latest uncompiled UI/audio changes and must not be treated as the
  current artifact.
- The cockpit glyph manifest now contains 102 unique characters. OPPO Sans cmap coverage
  is complete for both generated fonts; they remain 4 bpp and uncompressed.
- `git diff --check` passed.
- `node --check skyanchor-miniapp/miniprogram/utils/service-status.js` passed.
- `app_safety_takeover.c/.h`, `font_hud_cn`, `font_loading_cn`, and
  `font_safety_title_cn` match HEAD byte-for-byte; the safety UI/countdown function bodies
  have no diff.
- Static checks confirmed one task-intro API caller, removal of all boot-report preview
  symbols, fixed 860×300 bounds, and sufficient width for `TAG 65535`.
- No compile was run after the volume, HUD refinements, task handoff page, font additions,
  or power-on preview removal, per the user's explicit instruction.
- Hardware FPS, heap trend, rendered-glyph inspection, and safety visual/timing regression
  have not yet been run.
- Repository inspection confirmed that P4 currently publishes high-level task state but
  not the CH32 `proto_stage`; exact door/tray-stage animation would therefore require only
  a small additive cloud-payload bridge, not a P4 control-flow change.

## Session log

### 2026-07-16 — Persistent project handoff created

- User rejected backup-heavy and hidden-stability plans.
- User accepted the creative demonstration plan centered on transparent visual docking,
  evidence, safety challenges, and a digital twin.
- A dedicated branch and repository-local memory system were requested.

### 2026-07-16 — Ordinary P4 cockpit and receipt implemented

- Added read-only AI and dock configuration snapshots plus an ordinary cockpit view model.
- Replaced the ordinary preview's large persistent panel/debug line with compact top and
  bottom translucent strips, real four-corner Tag lines, a crosshair, one dominant guidance
  arrow, five state gates, and real guidance/metrics.
- Added generation-isolated normal-task evidence for AI confidence, Tag/distance/stability,
  CH32 weight, duration, and photo digest state.
- Added an 8-second ordinary completion receipt that is rejected for safety, failed,
  cancelled, or generation-mismatched tasks.
- Generated `font_cockpit_cn` (18 px) and `font_cockpit_title_cn` (26 px) from the repository
  OPPO Sans font without modifying safety fonts.
- Added safety-active early returns to ordinary update/receipt/snapshot entry points and
  kept the safety preview hide path at its original two LVGL object updates.
- At that point the exact next action was the target-hardware acceptance pass; it remains
  outstanding but is superseded as the immediate next effort by the authorized twin work.

### 2026-07-16 — Ordinary UI refinement and startup preview cleanup

- Aligned the ordinary HUD top and bottom panels to the same 18 px screen margin.
- Preserved the last real generation-owned distance/stability values after authorization,
  preventing the success HUD from reverting to `距离 --` when the Tag leaves the frame.
- Replaced the temporary white order transition with an opaque deep-navy 1024×600 page
  and centered 860×300 panel showing only `普通配送`, the real Tag, and three static stages.
- Added `app_ui_task_intro_view_t` with generation, independent target validity, and target
  ID; Tag 0 now renders as `TAG 0`. The 1.5-second dwell checks the owner every 50 ms.
- Extended the ordinary OPPO Sans glyph set from 93 to 102 unique non-whitespace
  characters and regenerated the 18 px/26 px 4 bpp uncompressed fonts. Safety fonts were
  not changed.
- Removed the power-on `配送报告` preview, its 9-second startup wait, preview-only UI API,
  placeholder values, and preview state. Real normal-task completion receipts remain.
- Set normal audio prompt volume to 75; safety missing/returned-drone prompts remain 65.
- Validation was static only: `git diff --check`, API/reference searches, layout/font-width
  checks, glyph cmap audit, and protected safety file hashes. No compile or hardware test
  was run.
- At that point the exact next action was to keep the worktree uncompiled until build
  authorization; the hardware acceptance work remains outstanding and is no longer the
  immediate next implementation action.

### 2026-07-16 - Implementation checkpoint and digital-twin handoff

- Committed the ordinary P4 cockpit, generation-isolated receipt, task handoff page, audio
  refinements, generated cockpit fonts, and Mini Program busy-state wording as `f0e6af0`.
- Static validation passed: whitespace checks, Mini Program JS syntax, 102-character font
  cmap audit, and protected safety source/font comparison. No P4 compile or hardware test
  was run.
- The user selected the Mini Program digital twin as the next effort. Phase 4 is now
  authorized; Phase 3 safety challenges remain deferred and the safety route remains frozen.
- The digital twin is Mini Program-first. P4 remains unchanged unless authentic fine-grain
  mechanical animation needs CH32 stage/weight data that is not in the current MQTT state;
  any such P4 work is limited to additive read-only cloud publishing fields.
- Exact next action is to build the order-detail twin view model and 2D WXML/WXSS/JS using
  existing real states, then assess whether a small payload-only P4 extension is necessary.

## Update template

Use this structure when saving future progress:

```text
Last updated:
Branch / HEAD:
Completed:
In progress:
Validation performed:
Known issues:
Decisions made:
Exact next action:
```
