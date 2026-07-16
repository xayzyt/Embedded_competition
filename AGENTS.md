# Repository Agent Instructions

## Required context at the start of every conversation

Before planning or changing this repository:

1. Read `docs/PROJECT_MEMORY.md` completely.
2. Read the active plan linked from that file.
3. Run `git status --short --branch` and confirm the current branch and uncommitted work.
4. Continue from the recorded "Next action" instead of rebuilding the project context from scratch.

The current active effort is the semifinal demonstration-experience work on
`feature/semifinal-demo-experience`.

## Persistent memory update rule

When the user says "更新记忆", "保存进度", or otherwise asks for a handoff:

1. Update `docs/PROJECT_MEMORY.md` before ending the task.
2. Record the date, branch, HEAD, completed work, current work, validation performed,
   unresolved issues, decisions made, and the exact next action.
3. Keep the memory factual. Do not mark work completed without code or validation evidence.
4. Preserve still-relevant completed milestones and replace stale next steps.
5. If the active implementation plan changes, update both the plan document and its
   summary in `docs/PROJECT_MEMORY.md`.

## Semifinal branch guardrails

- Preserve the existing delivery workflow, AI model and thresholds, AprilTag decision
  thresholds, CH32 protocol, and mechanical motion parameters unless the user explicitly
  changes scope.
- Prefer visible, authentic demonstration features over hidden diagnostics or simulated
  technical data.
- All displayed confidence, distance, weight, timing, and evidence values must come from
  actual runtime data.
- Do not implement fake AI overlays, fake fault results, or animations that imply a
  physical action different from the real device state.

