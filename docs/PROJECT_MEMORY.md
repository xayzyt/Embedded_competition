# SkyAnchor Project Memory

This file is the persistent handoff for future conversations. Read it before taking
action, then read the active plan linked below.

## Current context

- Last updated: 2026-07-16
- Active branch: `feature/semifinal-demo-experience`
- Branch base: `main@5fc0c28`
- Active plan: [Semifinal Demo Experience Plan](SEMIFINAL_DEMO_EXPERIENCE_PLAN.md)
- Current status: planning is accepted; implementation has not started.

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
4. Third priority: interactive judge safety challenges using real system inputs.
5. Fourth priority: lightweight Mini Program digital twin driven by real device state.
6. Edge-autonomous delivery during a cloud outage is optional and starts only after the
   first four items are stable.
7. Do not spend this effort on backup packages, generic health dashboards, broad
   refactoring, or invisible monitoring features.
8. Preserve the current business flow, model, thresholds, protocol, and mechanical
   parameters unless a later user decision explicitly changes them.

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
- [ ] Phase 1: transparent AI visual docking cockpit.
- [ ] Phase 2: trusted delivery evidence chain.
- [ ] Phase 3: judge safety challenge experience.
- [ ] Phase 4: Mini Program digital twin.
- [ ] Optional phase: cloud-outage edge autonomy and delayed synchronization.

## Next action

Start Phase 1 from the active plan:

1. Expose the latest real AI scores and inference metadata through a read-only public
   snapshot.
2. Define one cockpit view model that combines AI, vision, dock-judge, task, weather,
   and CH32 state without changing control decisions.
3. Add reusable LVGL overlay objects for the Tag quadrilateral, target crosshair,
   guidance arrows, distance band, stability progress, and authorization gates.
4. Limit display updates to 10 Hz and perform no allocation in the frame-update path.

## Validation state

- No code has been changed for the semifinal experience yet.
- No build or hardware test has been run for this branch yet.
- Documentation-only changes are the current branch delta.

## Session log

### 2026-07-16 — Persistent project handoff created

- User rejected backup-heavy and hidden-stability plans.
- User accepted the creative demonstration plan centered on transparent visual docking,
  evidence, safety challenges, and a digital twin.
- A dedicated branch and repository-local memory system were requested.

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

