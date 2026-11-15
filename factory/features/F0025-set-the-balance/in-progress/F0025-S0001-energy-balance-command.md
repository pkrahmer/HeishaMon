---
stage: review
blocked: null
round: 0
attempts: 0
crashes: 0
pull_request: 43
comments_read: []
handed_over: null
claim:
  instance: heishamon
  stage: review
  started_at: '2026-11-15T22:35:43.851471Z'
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
