# SkyAnchor Semifinal Demo Experience Plan

## 1. Product story and constraints

The semifinal build should communicate three ideas immediately:

1. The system exposes how it makes a decision instead of hiding AI behind a final motor
   action.
2. Judges can challenge the safety logic with real inputs and see why the system accepts
   or refuses delivery.
3. A completed delivery produces a verifiable evidence chain rather than only a final
   status label.

The working title is:

> SkyAnchor intelligent micro-port: visible decisions, judge-driven safety challenges,
> and verifiable delivery evidence.

Implementation constraints:

- Preserve the normal order, recognition, docking, mechanical, photo, and completion
  sequence.
- Do not change the AI model, sampling interval, recognition threshold, confirmation
  hits, AprilTag thresholds, distance gate, CH32 protocol, or motion parameters.
- All visible values must be real runtime values.
- UI animation must never drive business state or mechanical actions.
- Avoid per-frame LVGL allocation and avoid drawing directly into camera buffers.

### Current ordinary-mode scope lock (2026-07-16)

- The accepted implementation scope is the ordinary task handoff transition, P4 camera
  cockpit, and ordinary completion receipt only.
- The safety-takeover state machine, safety UI, 10-second countdown, typhoon callback,
  recovery/failure screens, safety fonts, CH32 safety actions, and safety preview branch
  are frozen and must remain visually and functionally unchanged.
- The ordinary preview uses a 52 px top strip, a 58 px bottom strip, a real Tag
  quadrilateral, one dominant-direction arrow, and textual distance/stability status.
  The earlier distance-band and stability-ring concepts are superseded by this compact
  layout so the 1024×600 camera view remains unobstructed.
- A normal order uses a 1.5-second dark cockpit-style handoff page after the camera is
  ready. It shows only the real task target and generation-bound state; Tag 0 is valid.
  The page is skipped for missing/stale tasks and for safety takeover.
- The temporary power-on delivery-report preview has been removed. The receipt is shown
  only for a real, generation-matched ordinary task that reaches `COMPLETED`.
- Phase 3, cloud evidence publishing, and the optional offline phase remain deferred. The
  user authorized Phase 4 Mini Program digital-twin work as the next effort on 2026-07-16.
  No safety-route UI work is authorized now.

Implementation status:

- Phase 1 ordinary P4 cockpit: implemented and builds successfully; hardware FPS/heap
  acceptance remains to be measured on the target board.
- Phase 2 ordinary P4 generation-isolated evidence trace and 8-second receipt: implemented
  and builds successfully; Mini Program/cloud extensions remain deferred.
- Latest ordinary UI refinements (distance retention, aligned HUD margins, task handoff
  page, font additions, and removal of the boot receipt preview) are implemented but have
  not been compiled because the user explicitly requested no build.
- Safety regression: protected source/font hashes match the branch HEAD; full visual and
  timing regression remains to be run on hardware.
- Phase 4 Mini Program digital twin: authorized and not yet implemented. It begins as a
  Mini Program-only change using existing high-level device/order states. A fine-grained
  P4 payload extension is conditional, additive, and limited to read-only telemetry.

### Ordinary task handoff transition

- Use a 1024×600 opaque deep-navy layer with one centered 860×300 panel.
- Show `普通配送`, `任务已接收`, the real `TAG N`, and the three static stages
  `任务接收 / 目标锁定 / 进入识别`; do not show hard-coded communication, weather,
  or camera states.
- Create all LVGL objects during UI initialization. Showing the page only updates a fixed
  target buffer, visibility, and z-order; it creates no objects and starts no animation.
- Use a view containing `generation`, `target_valid`, and `target_id`. Poll the owning task
  every 50 ms during the 1.5-second dwell so cancellation, replacement, or safety takeover
  invalidates the page promptly.
- Use only `font_cockpit_cn`, `font_cockpit_title_cn`, and the existing ASCII title font.
  Safety fonts remain unchanged.

## 2. Phase 1 — Transparent AI visual docking cockpit

### Experience

Turn the current preview into an edge-AI cockpit. A judge holding the drone or Tag should
see the system react immediately:

- A real four-corner outline follows the detected AprilTag.
- A target crosshair shows the desired docking center.
- Direction guidance shows how the target should move.
- A distance band shows too far, acceptable, or too near.
- An AI confidence gauge shows the current drone classification score.
- A stability ring fills from the real stable-frame count.
- Authorization gates illuminate as real conditions become true.

The authorization gates are fixed as:

1. Task authorized.
2. Drone confirmed by edge AI.
3. Tag identity matched.
4. Position and distance acceptable.
5. Stable observation and weather permission.

When all gates pass, the target outline changes from yellow to green and the UI displays
"Identity, position, and environment verified — delivery permitted". The existing control
logic remains the only authority that starts docking.

### Internal interfaces

Add a read-only AI result snapshot containing:

- validity and current class;
- drone and no-drone scores;
- motion score;
- current hit count and required hit count;
- confirmation state;
- latest inference duration and result timestamp.

Add a cockpit view model combining:

- task target and task state;
- AI snapshot;
- `app_vision_result_t` corners and detection metadata;
- filtered dock-judge result;
- weather permission;
- CH32 stage and weight when available.

The view model is generated by the control/UI adapter. The UI must not query or mutate
control internals.

### Drawing rules

- Pre-create four LVGL lines for the Tag quadrilateral, one crosshair, guidance labels,
  five gate indicators, one distance bar, and one stability ring.
- Reuse those objects for the lifetime of the preview screen.
- Update the cockpit at a maximum of 10 Hz.
- Hide the Tag outline after the existing lost-frame hold expires.
- Use the display-mapped corner coordinates already returned by the vision pipeline.
- If the Tag center is left of the allowed region, tell the operator to move the target
  right; apply the inverse instruction for the other three directions.
- Distance above the configured maximum displays "move closer"; below the minimum
  displays "move farther".
- Label softmax output as "model confidence", not universal recognition accuracy.

### Acceptance

- Moving Tag 0 or Tag 1 produces a stable, correctly oriented quadrilateral.
- Guidance agrees with the actual direction needed to enter the configured center zone.
- Wrong identity, bad distance, unstable observation, and bad weather visibly prevent
  the final gate.
- No cockpit element changes a dock-judge result or task transition.
- Preview remains smooth and no LVGL object is created during periodic updates.

## 3. Phase 2 — Trusted delivery evidence chain

### Experience

Present every completed delivery as five linked pieces of evidence:

1. Cloud task authorization.
2. Edge-AI drone confirmation.
3. AprilTag identity and docking confirmation.
4. Physical cargo confirmation from HX711.
5. Delivery photo and SHA-256 digest.

After a real ordinary task reaches `COMPLETED`, the P4 displays a receipt for 8 seconds
containing:

- order name or ID suffix;
- AI model confidence captured at confirmation;
- matched Tag ID;
- final estimated distance and stable-frame count;
- confirmed cargo weight;
- total task duration;
- photo status and the first eight hexadecimal digest characters.

The receipt then returns to the existing main screen automatically. It is never shown at
power-on and is never shown for safety, failed, cancelled, or generation-mismatched tasks.

### Data model

Add one per-task trace owned by the control layer:

```c
typedef struct {
    char order_id[48];
    uint32_t generation;
    uint16_t target_id;
    float ai_confidence;
    uint32_t ai_confirm_ms;
    uint16_t matched_tag_id;
    int32_t final_distance_mm;
    uint16_t stable_frames;
    int32_t delivery_weight_g;
    uint32_t total_duration_ms;
    char photo_sha256[65];
} app_demo_trace_t;
```

Capture values only at their authoritative events:

- AI confidence when AI confirmation first becomes true.
- Tag and docking values when authorization passes.
- Weight from the CH32 cargo-confirmed status.
- Duration when the task reaches its terminal state.
- SHA-256 when photo encoding completes.

Reset the trace only when a new task generation starts. Late photo completion may update
only the matching generation and order ID.

### Cloud and Mini Program

Add an optional, backward-compatible `evidence` object to the retained state. Existing
fields remain unchanged.

Upgrade the order timeline to show real evidence events with timestamps and values.
Upgrade the delivery report to show the five evidence stages and the photo digest prefix.
If the photo is still uploading, show "evidence generated, synchronization pending"
instead of hiding the receipt.

### Acceptance

- Every displayed receipt value can be traced to a real module event.
- A late photo result cannot attach to the next order.
- Failed or cancelled tasks never display a successful receipt.
- The Mini Program still works when the additive evidence object is absent.

## 4. Phase 3 — Judge safety challenge experience

### Experience

Upgrade the existing exception/safety presentation into a judge-facing challenge page.
The page offers three real scenarios and one optional scenario:

1. Identity mismatch.
2. Sudden typhoon protection.
3. Cargo not delivered.
4. Optional cloud outage and edge autonomy.

Every scenario uses the same three-part explanation:

```text
Observed condition -> System decision -> Safety action
```

No scenario may report a simulated success that did not occur in the underlying state
machine.

### Identity mismatch

- Start a task for Tag 1 and present Tag 0, or vice versa.
- Show detected and authorized IDs side by side.
- Display "delivery refused" and "door remains closed" while the real gate rejects it.
- Do not add a software-only fake mismatch result.

### Sudden typhoon

- Reuse the existing weather simulation and safety-takeover path.
- Show the authorization gates changing when weather permission is withdrawn.
- Display the actual CH32 safety stage while the mechanism closes.
- The final result must distinguish "safely closed" from "safety action failed".

### Cargo not delivered

- Use the real HX711 weight value and current cargo threshold.
- While below the threshold, show that completion is intentionally withheld.
- Let the operator invoke the existing fallback retract action.
- Present the result as a controlled exception path, not a delivery success.

### Recovery

- Reuse and visually clarify the existing demo-reset action.
- The challenge page returns to the known main-screen state only after the current reset
  path reports success.
- Do not add direct motor buttons to the judge page.

### Acceptance

- Each challenge is driven by real input and real task state.
- The screen clearly states why delivery was refused or interrupted.
- One scenario cannot leave weather, target, AI gate, or safety-overlay state active in
  the next scenario.

## 5. Phase 4 – Mini Program digital twin

### Scope boundary (authorized 2026-07-16)

- Build the view and all visual animation in the Mini Program order-detail page first.
- The current P4 MQTT snapshot already reports high-level task state, target, matched Tag,
  cargo completion, fault, weather, photo, and order identity. That is enough for a truthful
  first version and does not require a P4 change.
- Exact door/tray/cargo animation requires the CH32 protocol stage, which P4 currently uses
  internally but does not publish in its MQTT task snapshot. If that fidelity is required,
  add only backward-compatible read-only fields such as `mechanical_stage` and real
  `weight_g` in the P4 cloud publisher and pass them through the cloud function.
- Do not modify the P4 display, control state machine, AI model/thresholds, AprilTag gates,
  CH32 commands/protocol, mechanical parameters, safety takeover, or timing for the twin.
- Never synthesize a physical stage from elapsed time. Missing, old, or unknown telemetry
  renders the twin in a neutral waiting state.

### Experience

Add a lightweight two-dimensional micro-port twin to the order detail page using WXML and
CSS rather than a 3D engine. It contains:

- drone;
- docking area;
- door;
- tray;
- cargo;
- cloud connection;
- weather overlay.

Map existing real states directly:

| Device state | Twin behavior |
| --- | --- |
| WAIT_APPROACH | Drone hovers above the port |
| AUTH_PASSED | Recognition ring becomes green |
| DOOR_OPENING | Door animates open |
| TRAY_EXTENDING | Tray moves outward |
| WAIT_CARGO | Cargo position pulses |
| RETRACTING | Tray returns with cargo |
| COMPLETED | Port and evidence marker become green |
| FAULT | The affected stage becomes red |
| WEATHER_BLOCKED | Wind/rain overlay and lock appear |

Animation is visual only. Device state remains authoritative, and unknown stages display
a neutral waiting state rather than guessing.

### Acceptance

- The twin never advances ahead of the reported device state.
- Polling updates do not restart an animation from the beginning unnecessarily.
- The order page remains usable on a normal phone without frame drops.

## 6. Optional phase — Cloud-outage edge autonomy

Begin this phase only after Phases 1–4 are stable.

Behavior:

- After a task is accepted, loss of MQTT prevents new tasks but does not discard the
  active local task.
- Existing local AI, Tag, weather snapshot, state machine, and CH32 control may finish the
  current task.
- If weather was already blocking delivery, offline mode cannot override it.
- The P4 displays "cloud offline / edge autonomous / new tasks paused".
- Final state and photo remain pending and synchronize after reconnect.
- Reconnect publishes the current snapshot; it never replays mechanical commands.
- No unfinished task is resumed after a P4 reboot.

This phase must be removed from the semifinal build if reconnection can duplicate commands
or associate evidence with the wrong order.

## 7. Implementation order

Current execution order: Phase 4 is the next authorized slice. Phase 3 remains deferred,
so implementing the digital twin does not authorize safety-route changes.

### Hours 0–16

- Expose the real AI live result.
- Build the cockpit view model.
- Implement Tag outline, crosshair, guidance, distance, stability, and authorization gates.

### Hours 16–26

- Add the per-task trace.
- Build the P4 completion receipt.
- Publish additive evidence data and upgrade the Mini Program report/timeline.

### Hours 26–38

- Build the three real judge challenges.
- Integrate existing weather, wrong-ID, no-cargo, fallback-retract, and demo-reset paths.

### Hours 38–48

- Build the Mini Program digital twin.
- Unify terminology, colors, animation timing, and transitions across P4 and Mini Program.
- Remove temporary debug presentation elements.

### After hour 48

- Implement cloud-outage edge autonomy only if the first four phases remain reliable.

## 8. Final on-site sequence

1. Create and start a normal order from the Mini Program.
2. Let a judge move the drone or Tag while the P4 cockpit gives live guidance.
3. Show five real authorization gates becoming green.
4. Complete the physical delivery and display the evidence receipt.
5. Show the synchronized Mini Program timeline and digital twin.
6. Let the judge select one safety challenge.
7. Demonstrate the real refusal or takeover decision and return through demo reset.

The three messages the judge should remember are:

> The system shows how it decides.  
> The judge can challenge its safety logic.  
> Every delivery produces verifiable evidence.
