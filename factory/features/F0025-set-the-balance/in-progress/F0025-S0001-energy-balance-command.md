---
stage: doing
blocked: null
round: 0
attempts: 0
crashes: 0
pull_request: null
comments_read: []
handed_over: null
claim:
  instance: heishamon
  stage: doing
  started_at: '2026-11-15T22:28:38.341455Z'
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
