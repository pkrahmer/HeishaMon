---
stage: doing
blocked: null
round: 1
attempts: 0
crashes: 0
pull_request: 43
comments_read: []
handed_over: null
claim:
  instance: heishamon
  stage: doing
  started_at: '2026-11-15T22:36:50.843121Z'
  lease_duration: 60
journal:
- number: 1
  speaker: human
  kind: created
  at: '2026-11-15T22:28:38.335294Z'
  stage: doing
- number: 2
  speaker: factory
  kind: started
  at: '2026-11-15T22:28:38.341455Z'
  stage: doing
- number: 3
  speaker: builder
  kind: result
  at: '2026-11-15T22:35:38.997973Z'
  stage: doing
  run: b4ee3a385c9f4a62928b7a8c358985ed
- number: 4
  speaker: factory
  kind: started
  at: '2026-11-15T22:35:43.851471Z'
  stage: review
- number: 5
  speaker: reviewer
  kind: result
  at: '2026-11-15T22:36:45.892307Z'
  stage: review
  run: 668f12a86f444c86878ee23f8109f546
- number: 6
  speaker: factory
  kind: started
  at: '2026-11-15T22:36:50.843121Z'
  stage: doing
---

# The energy balance command

## Description

**Why.** A restart resets the energy balance to 0, and the pump then rests until the balance reaches the start threshold again. The owner wants to set the balance back after a restart, from the value recorded before it.

**What it does.** A message on `<base>/control/set/energy_wh` whose payload is a finite number sets the controller's energy balance to that value in Wh, clamped to the anti-windup bounds of the directions allowed at that moment (the same bounds the controller's own integration uses). It first advances the controller to the moment of the message, as every other input does, so that no interval is lost or counted twice. The next decision uses the new balance. A payload that is not a finite number is refused and changes nothing. The command is not stored, is not a setting, and changes no setting; it is handled like `integral_reset`.

**Telemetry.** The next status publication shows the new `status/energy_wh`. A refused payload is logged like a refused setting.

## Interface

- The core: a function that advances to `t` and then sets the energy, clamped, beside `ctl_reset_integral`, with its rules in the header comment.
- The glue: the command topic in the dispatch, its handling in the firmware glue.
- Tests: a new host test `tests/control/test_set_energy.c`. The leave for tests another story wrote: `tests/control/test_dispatch.c` may gain rows for the new topic; no check may be removed or loosened.
- No upstream file changes, and no hook line changes.

## Acceptance criteria

1. With hysteresis 1500 Wh and energy clamp factor 2, every direction allowed, rows of one test: setting -1200 gives -1200.0; setting -5000 gives -3000.0; setting 5000 gives 3000.0; setting NaN or infinity is refused and keeps the balance.
2. With only heating and no demand allowed, setting 2000 gives the upper bound of that mask, the same value `ctl_set_allowed` would clamp to.
3. Setting the balance at `t` integrates the interval up to `t` first: a test with a known net power shows the energy before the set includes that interval, and the interval after the set starts from the set value.
4. A message on `<base>/control/set/energy_wh` reaches the command; nothing is written to `/control.json`.
5. Every golden trace replays bit for bit, as before.
6. `make firmware` builds both boards.
7. `git diff develop --stat -- HeishaMon ':!HeishaMon/src'` shows no upstream file changed.
8. `make check` stays green.
9. docs: `docs/control/settings.md` describes the command beside `integral_reset`; `docs/control/core.md` the new function; `docs/control/decisions.md` gets one entry: a one-shot command to set the balance after a restart, clamped to the controller's bounds; rejected: restoring it automatically at boot (a stale value after a long outage would be wrong).

## Smoke test

```sh
make test 2>&1 | grep -E '^control: [0-9]+ test programs passed$' | wc -l
```

Expect: `1`

## Journal

1. 2026-11-15 human: created.
2. factory: started doing as heishamon.
3. builder: Built the energy balance command: tests first (they failed to compile without the new names), then the code. `make check` is green (lint, test with 36 control programs, firmware for both boards).

   What I built:
   - Core: `ctl_set_energy(state, energy_wh, t, config)` in `HeishaMon/src/control/core/ctl_controller.{h,c}`, next to `ctl_reset_integral`. It refuses a NaN or infinite `energy_wh` or `t` and returns false without changing anything, not even `t_last`. Otherwise it calls `ctl_advance` to `t`, then sets the energy, clamped through the same `clamp_to_allowed` the integration uses, and returns true. The header comment now says "the functions" instead of "the five functions".
   - Dispatch: `HeishaMon/src/control/glue/ctl_dispatch.{h,c}` has a new `CTL_TOPIC_SET_ENERGY` for `<base>/control/set/energy_wh`, handled like `integral_reset`. A new pure function, `ctl_dispatch_energy_wh(payload, length, &energy_wh)`, reads the payload. It accepts only a bare finite number: it uses `ctl_payload_parse` and refuses a JSON object, the same way the settings and integral-store parsers do.
   - Firmware glue: `handle_set_energy` in `HeishaMon/src/control/glue/ctl_firmware.cpp` and a new case in `ctl_firmware_mqtt`. It reads the payload before any log line can overwrite the buffer. A refused payload is logged as `control: setting energy_wh refused (BAD_VALUE)`, the same form as a refused setting. A taken one is logged as `control: energy balance set to <clamped value> Wh`. Nothing is saved: no settings file and no integral file.
   - Tests: new `tests/control/test_set_energy.c`:
     - criterion 1: rows for -1200, -5000 and 5000, and refusal of NaN and ±infinity;
     - criterion 2: heating-only mask, checked against a twin state that `ctl_set_allowed` clamped;
     - criterion 3: compared with a twin that advanced to the same `t` without a set, the integrated seconds and ledger match; the next interval starts from -1200 + net·h;
     - the next decision uses the new value;
     - criterion 4: dispatch kind, and `energy_wh` is no setting;
     - payload rows.
     
     `tests/control/test_dispatch.c` gained three rows, as the story allows; no check was removed.
   - Docs: in `docs/control/settings.md`, a new section "Setting the energy balance" after the integral reset, and the cross-reference now reads *The six functions*. In `docs/control/core.md`, the six-function count, the new function, the `energy_wh` row and the order of calls. In `docs/control/decisions.md`, one new entry.

   Decisions where the story left room (all recorded in decisions.md):
   - The payload must be a bare number; a JSON object is refused.
   - A non-finite `t` is refused too, so a refusal changes nothing at all.
   - The `clamped` flag is left alone, because it reports the last integration step.
   - An out-of-bounds value is clamped, not refused.
   - Rejected: restoring the balance automatically at boot, and refusing out-of-bounds values.

   Smoke test: `make test 2>&1 | grep -E '^control: [0-9]+ test programs passed$' | wc -l` printed `1`, as expected. `git diff develop --stat -- HeishaMon ':!HeishaMon/src'` is empty. Golden traces: test_traces passes and was not changed.

   Noticed but not touched: if the board has never seen a `t` before, the first set only seeds `t_last`, and the balance it sets carries through the seeding. A later FAULT recovery still resets the balance to 0, as documented.

   `check`: check: green
4. factory: started review as heishamon.
5. reviewer: Reviewed `git diff develop...HEAD` against the story and checked it in the code: `ctl_set_energy` uses `ctl_advance`, then `clamp_to_allowed`/`ctl_clamp_bounds` (permitted direction ±factor·hysteresis, masked direction ±hysteresis, as settings.md says). The handler in the firmware glue saves nothing, and its refusal log uses the same form as `handle_setting`. Telemetry publishes `status/energy_wh` from the next decision after the next `ctl_loop_tick`, so the next publication shows the value that was set. Each criterion has a test: 1 is in the rows test plus the refusal test, 2 is checked against a twin clamped by `ctl_set_allowed`, 3 is checked against a twin that only advanced (integrated_s and ledger) plus the next interval from -1200, 4 checks the dispatch kind and that no `energy_wh` setting exists, and 5 and 6 pass because the factory's `make check` was green. The clamp is tested on both sides (-1200 inside, ±5000 outside). Non-finite energy and t are refused. The story's interface names the core, dispatch and firmware glue files, and no upstream file or hook line changed. The decision entry covers the main choice and what was rejected.

   Findings:
   1. `HeishaMon/src/control/glue/ctl_dispatch.c:86` (`ctl_dispatch_energy_wh`): copies the existing public `ctl_integral_store_parse` (`HeishaMon/src/control/glue/ctl_integral_store.c:23`). Both refuse a '{', both call `ctl_payload_parse` and both check `isfinite`, and the private `parse_bare_number` in `ctl_settings.c:165` is a third copy. The rule is "no second copy of an existing helper". Fix: move the bare-number parse into one shared function in `HeishaMon/src/control/adapt/ctl_payload.{h,c}` (refuse an object and a non-finite value) and call it from `ctl_dispatch_energy_wh`. The NULL check can stay in the dispatch. Alternatively, have the dispatch call the existing helper and give it a name that is not tied to the integral store. The payload rows in `tests/control/test_set_energy.c` stay as they are.

   Earlier tests:
   - `tests/control/test_dispatch.c` (M): three rows added to `test_topics`, under the leave the story gives by name. No earlier check was removed or loosened.
6. factory: started doing as heishamon.
