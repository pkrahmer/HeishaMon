---
stage: approval
blocked: null
round: 1
attempts: 0
crashes: 0
pull_request: 5
comments_read: [6806361194]
handed_over: approval
claim: null
journal:
- number: 1
  speaker: human
  kind: created
  at: '2026-11-11T20:02:18.750311Z'
  stage: doing
- number: 2
  speaker: factory
  kind: started
  at: '2026-11-11T20:02:18.756524Z'
  stage: doing
- number: 3
  speaker: builder
  kind: result
  at: '2026-11-11T20:06:14.417157Z'
  stage: doing
  run: e04c5a89206c4e03bccff1ef1a668a1f
- number: 4
  speaker: factory
  kind: started
  at: '2026-11-11T20:06:21.680011Z'
  stage: review
- number: 5
  speaker: reviewer
  kind: result
  at: '2026-11-11T20:07:46.469172Z'
  stage: review
  run: 801f139300a84fcd880018c7f86e0e99
- number: 6
  speaker: factory
  kind: started
  at: '2026-11-11T20:07:52.333835Z'
  stage: doing
- number: 7
  speaker: builder
  kind: result
  at: '2026-11-11T20:09:40.789828Z'
  stage: doing
  run: fd965028e1864583a58cfd5898dd9c3e
- number: 8
  speaker: factory
  kind: started
  at: '2026-11-11T20:09:46.796929Z'
  stage: review
- number: 9
  speaker: reviewer
  kind: result
  at: '2026-11-11T20:11:12.575529Z'
  stage: review
  run: ead775bf63184d19b58047db39129103
- number: 10
  speaker: factory
  kind: started
  at: '2026-11-11T20:11:18.047076Z'
  stage: doing
- number: 11
  speaker: builder
  kind: result
  at: '2026-11-11T20:13:15.502260Z'
  stage: doing
  run: 8189f6c3a7844806befafb6ba47b0788
- number: 12
  speaker: factory
  kind: started
  at: '2026-11-11T20:13:21.212042Z'
  stage: review
- number: 13
  speaker: reviewer
  kind: result
  at: '2026-11-11T20:14:58.451194Z'
  stage: review
  run: 40acf723c3044c1ab724742d6a4f7cdb
- number: 14
  speaker: factory
  kind: started
  at: '2026-11-11T20:15:03.931368Z'
  stage: doing
- number: 15
  speaker: builder
  kind: result
  at: '2026-11-11T20:16:12.027909Z'
  stage: doing
  run: 93f245fa73d04fd8a540961e2837bc9e
- number: 16
  speaker: factory
  kind: started
  at: '2026-11-11T20:16:17.516847Z'
  stage: review
- number: 17
  speaker: reviewer
  kind: result
  at: '2026-11-11T20:17:42.159142Z'
  stage: review
  run: c808905e9eb34ac98f6a252f2ec11752
- number: 18
  speaker: factory
  kind: question
  at: '2026-11-11T20:17:42.159142Z'
  stage: review
  run: c808905e9eb34ac98f6a252f2ec11752
  blocked: round_cap
- number: 19
  speaker: human
  kind: comment
  at: '2026-11-11T20:21:31.387285Z'
  stage: review
- number: 20
  speaker: factory
  kind: started
  at: '2026-11-11T20:21:34.523787Z'
  stage: review
- number: 21
  speaker: reviewer
  kind: result
  at: '2026-11-11T20:22:00.972273Z'
  stage: review
  run: c43de41673374bacbf4935937ca64135
- number: 22
  speaker: factory
  kind: started
  at: '2026-11-11T20:22:06.432335Z'
  stage: doing
- number: 23
  speaker: builder
  kind: result
  at: '2026-11-11T20:22:56.621032Z'
  stage: doing
  run: 0f0237c3ddab4f368f975197ff64777e
- number: 24
  speaker: factory
  kind: started
  at: '2026-11-11T20:23:03.700510Z'
  stage: review
- number: 25
  speaker: reviewer
  kind: result
  at: '2026-11-11T20:23:31.311145Z'
  stage: review
  run: 80897d52f4594971b37ba189567d382d
---

# The controller: advance, observe, set_allowed and evaluate

## Description

The control core gets its controller: one state struct and four functions that move it. `ctl_advance` integrates the energy balance over the interval that just closed, `ctl_observe` takes one reading, `ctl_set_allowed` changes which directions are permitted, and `ctl_evaluate` decides the demand and explains it in a decision. The owner needs it as the last part of the core before the golden traces are replayed through it in the next story.

The module is a port of `docs/control/python-reference/controller.py`: `ControllerState`, `initial_state`, `advance_to`, `observe`, `set_allowed` and `evaluate`, and of `Decision` in `types.py`. `docs/control/reference.md` sections 2.5, 2.6, 4 and 7 describe them. Every expression keeps the operand order of the Python source, because the golden traces are compared bit for bit. The golden traces of the next story are the final measure: **where a criterion of this story and the Python source disagree, the Python source wins**, and the builder names the criterion in its entry instead of asking.

Not ported: `restore`, `to_snapshot`, `observe_all` and the mutable `Controller` class. Left to the next story: the trace runner. The story changes nothing in the modules the earlier stories built.

## Interface

New files: `HeishaMon/src/control/core/ctl_controller.h`, `ctl_controller.c`. Test file: `tests/control/test_controller.c`, with the check helper as it is. A test may set fields of a `ctl_state` directly to arrange a case.

```c
typedef struct {
    ctl_phase phase;
    double energy_wh;
    ctl_demand demand;
    uint8_t allowed;      /* CTL_DEMAND_BIT of each permitted demand */
    bool has_t_last;
    double t_last;        /* the integration reference */
    bool has_t_healthy;
    double t_healthy;     /* the last instant every required signal resolved */
    ctl_signal_table signals;
    double integrated_s;
    double skipped_s;
    ctl_ledger ledger;
    bool clamped;
} ctl_state;

typedef struct {
    double t;
    ctl_demand demand;
    ctl_reason reason;
    ctl_phase phase;
    double energy_wh;
    double hysteresis_wh;
    double dt_s;
    double integrated_s;
    double skipped_s;
    bool has_inputs;      /* false: inputs and balance hold nothing */
    ctl_inputs inputs;
    ctl_balance balance;
    ctl_ledger ledger;
    bool clamped;
    uint8_t unhealthy;    /* CTL_SIGNAL_BIT of each required signal that is not fresh */
    uint8_t substituted;  /* CTL_SIGNAL_BIT of each optional signal that fell back */
} ctl_decision;

void ctl_state_init(ctl_state *state);
bool ctl_advance(ctl_state *state, double t, const ctl_config *config, double *dt_s);
void ctl_observe(ctl_state *state, ctl_signal signal, double value, double t, const ctl_config *config);
void ctl_set_allowed(ctl_state *state, uint8_t allowed, double t, const ctl_config *config);
void ctl_evaluate(ctl_state *state, double t, const ctl_config *config, ctl_decision *decision);
```

`ctl_state_init`: phase `UNINITIALIZED`, energy 0.0, demand `NONE`, `allowed` `CTL_DEMAND_ALL`, no `t_last`, no `t_healthy`, an empty signal table, both second counters 0.0, a zero ledger, `clamped` false.

`ctl_advance` moves the integration reference to `t`. It returns whether it integrated, and it writes the length of the interval it closed to `*dt_s`.

- A `t` that is NaN or infinite changes nothing: false, `*dt_s` 0.0.
- Without a `t_last` it sets `t_last` to `t`: false, 0.0.
- With `dt = t - t_last` at or below 0.0 it changes nothing: false, 0.0.
- Otherwise it resolves the signals at `t_last`, the start of the interval. When they do not resolve, or when `dt > max_step_s`, it sets `t_last` to `t` and adds `dt` to `skipped_s`: false, `dt`.
- Otherwise it integrates: `hours = dt / 3600.0`; `raw = energy_wh + p_net_w * hours`; the energy becomes `raw` clamped to the bounds of `ctl_clamp_bounds(allowed)`; `integrated_s` grows by `dt`; the ledger accumulates the inputs and the balance over `hours`; `clamped` becomes whether the clamped energy differs from `raw`; `t_last` becomes `t`: true, `dt`.

`ctl_observe` takes one reading.

- A value or an instant that is NaN or infinite changes nothing.
- A reading older than the one the table holds for the same signal changes nothing. One at the same instant is taken.
- Otherwise it first calls `ctl_advance` to the reading's instant, which closes the open interval with the old value, and then stores the reading. A reading that lies before `t_last` is therefore stored without any integration and without moving `t_last`.

`ctl_set_allowed` changes the permitted directions.

- The new mask is `allowed` with the bit of `NONE` added.
- It calls `ctl_advance` to `t` first, also when the mask does not change.
- When the new mask differs from the state's, it stores it and clamps the energy to the new bounds. It does not touch `clamped`.

`ctl_evaluate` advances to `t`, decides, and fills the decision. It is the only function that changes `demand`.

- A `t` that is NaN or infinite changes nothing in the state. The decision carries `t`, the state's demand, phase, energy, counters, ledger and `clamped`, the reason `INVALID_TIME`, `dt_s` 0.0, `has_inputs` false, and 0 for both masks.
- Otherwise it calls `ctl_advance` to `t`, then resolves the signals at `t`. The decision's `dt_s` is what `ctl_advance` wrote; `has_inputs`, `inputs`, `balance` and `substituted` come from that resolution (`substituted` is 0 when it fails); `unhealthy` is `ctl_signals_unhealthy` at `t`; the other fields are read from the state after the step below.
- Phase `UNINITIALIZED`, signals not resolved: demand `NONE`, reason `WAITING_FOR_INPUTS`, state unchanged.
- Phase `UNINITIALIZED`, signals resolved: the phase becomes `RUNNING` and `t_healthy` becomes `t`; the decision carries the state's demand and the reason `SEEDED`.
- Phase `FAULT`, signals resolved: the phase becomes `RUNNING`, the energy 0.0, the demand `NONE` and `t_healthy` `t`; reason `FAULT_RECOVERED`. `clamped` stays as it was.
- Signals not resolved, in any other case: the time since `t_healthy` is `t - t_healthy`, or infinite without a `t_healthy`. When the phase is not `FAULT` and that time is at most `input_hold_s`, the phase becomes `HOLD` and the decision carries the state's demand with the reason `HELD_STALE_INPUT`. Otherwise the phase becomes `FAULT` and the demand `NONE`, with the reason `FAULT_TIMEOUT`.
- Otherwise, the normal case: the demand is `ctl_relay(energy_wh, demand, hysteresis_wh)` with the reason `NORMAL`; when the mask lacks that demand's bit, it is `NONE` with the reason `MASKED`. The phase becomes `RUNNING`, the state's demand the result, and `t_healthy` `t`.

Differences from the Python reference, all decided:

- The state is a struct the caller owns and the four functions change in place.
- `None` for `t_last` and `t_healthy` is a flag beside the value; `None` for the decision's inputs and balance is `has_inputs` false.
- `observe` takes the three parts of a reading as arguments.
- No function takes a null pointer or a signal outside the enum; the caller guarantees both.

## Acceptance criteria

The cases use the default configuration. "The three readings" are `T_INDOOR` 21.0, `T_OUTDOOR` 5.0 and `P_PRODUCED` 0.0. With them and the fallbacks, the net power is -2735.0 W (transmission -2480.0, model -1930.0, trim -805.0).

1. `ctl_state_init` sets every field as named above.
2. `ctl_advance` without integration, as rows of one table test: NaN and `INFINITY` as `t` return false with 0.0 and leave `t_last` unset; the first finite `t` sets `t_last` and returns false with 0.0; after that, the same `t` and an earlier `t` return false with 0.0 and leave `t_last` and both counters as they were.
3. `ctl_advance` skips an interval it cannot trust: with `t_last` 0.0 and no signal, advancing to 100.0 returns false with 100.0, sets `t_last` to 100.0 and `skipped_s` to 100.0, and leaves the energy at 0.0. With the three readings observed at 0.0, advancing from 0.0 to the next double above 300.0 skips, and advancing from 0.0 to exactly 300.0 integrates.
4. `ctl_advance` integrates: with the three readings observed at 0.0 and `t_last` 0.0, advancing to 300.0 returns true with 300.0. The energy equals `0.0 + -2735.0 * (300.0 / 3600.0)`, `integrated_s` is 300.0, `skipped_s` 0.0, `clamped` false, and the ledger holds, with `h = 300.0 / 3600.0`: produced 0.0, solar 0.0, internal `550.0 * h`, transmission `-2480.0 * h`, trim `-805.0 * h`.
5. The clamp in `ctl_advance`: as criterion 4 but with the state's energy set to -2990.0 beforehand, the energy after the step is -3000.0 and `clamped` is true. Then `P_PRODUCED` 5000.0 is observed at 300.0 and the state advanced to 600.0: the energy is `-3000.0 + 2265.0 * (300.0 / 3600.0)` and `clamped` is false.
6. `ctl_observe` refuses, as rows of one table test, each leaving every field of the state as it was: a NaN value, an infinite value, a NaN instant, an infinite instant, and a reading of `T_INDOOR` at 50.0 when the table holds one at 100.0. A reading of `T_INDOOR` at exactly 100.0 with another value is taken.
7. `ctl_observe` closes the interval with the old value: with the three readings observed at 0.0, observing `T_INDOOR` 22.0 at 300.0 leaves the energy of criterion 4, and the table then holds 22.0 at 300.0.
8. `ctl_observe` of a reading before the reference: with `t_last` 300.0 and `T_OUTDOOR` held from 0.0, observing `T_OUTDOOR` 4.0 at 200.0 stores it and leaves `t_last`, the energy and both counters as they were.
9. `ctl_set_allowed`: a mask of `HEATING` alone is stored as `HEATING` and `NONE`. With the energy set to -2000.0, a mask of `COOLING` alone makes the energy -1500.0; widening the mask back to all three leaves it at -1500.0. With the three readings observed at 0.0 and `t_last` 0.0, a call at 300.0 with the unchanged mask still integrates as in criterion 4. `clamped` is false before and after each of these calls that does not integrate.
10. `ctl_evaluate` with a NaN and with an infinite `t`, on a state arranged as `RUNNING` with demand `HEATING` and energy -100.0: the state keeps every field, and the decision carries the reason `INVALID_TIME`, demand `HEATING`, phase `RUNNING`, energy -100.0, `dt_s` 0.0, `has_inputs` false and both masks 0.
11. The cold start: `ctl_evaluate` at 0.0 on a fresh state gives `NONE`, `WAITING_FOR_INPUTS`, phase `UNINITIALIZED`, `has_inputs` false, and `unhealthy` with all three required bits. After the three readings are observed at 0.0, `ctl_evaluate` at 300.0 gives `NONE`, `SEEDED`, phase `RUNNING`, `dt_s` 300.0, the energy of criterion 4 (the interval is integrated before the seeding), `has_inputs` true with `p_net_w` -2735.0, `unhealthy` 0, `substituted` with both optional bits, and the state's `t_healthy` is 300.0.
12. The normal case and the relay: on the state after criterion 11, with the energy set to -1500.0, `ctl_evaluate` at 300.0 again gives `HEATING` with `NORMAL` and `dt_s` 0.0, and the state's demand is `HEATING`. With the energy then set to 0.0, the next one at 300.0 gives `NONE` with `NORMAL`.
13. The mask: on the state after criterion 11, with the mask set to `COOLING` and `NONE` and the energy set to -1500.0, `ctl_evaluate` at 300.0 gives `NONE` with `MASKED`, and the state's demand is `NONE`.
14. Hold, fault and recovery, on the state after criterion 11 with the demand set to `HEATING` and the energy set to -500.0; the readings are from 0.0 and `t_healthy` is 300.0:
    - `ctl_evaluate` at 700.0 gives `HEATING`, `HELD_STALE_INPUT`, phase `HOLD`, `has_inputs` false, `unhealthy` with all three required bits, and the energy still -500.0 with `skipped_s` 400.0.
    - At 1200.0 (900.0 after `t_healthy`, the limit itself) it gives the same demand, reason and phase.
    - At the next double above 1200.0 it gives `NONE`, `FAULT_TIMEOUT`, phase `FAULT`, and the state's demand is `NONE`.
    - At 1300.0, still without readings, it gives `FAULT_TIMEOUT` and `FAULT` again.
    - After the three readings are observed at 1400.0, `ctl_evaluate` at 1400.0 gives `NONE`, `FAULT_RECOVERED`, phase `RUNNING`, energy 0.0, and `t_healthy` 1400.0.
15. `make check` stays green.
16. docs: `docs/control/core.md` gets a section on the controller after the sections it has: the state and what each field means, the four functions with what each refuses, the phases with the transitions between them as a table (from, condition, to, reason), and the order of calls a host makes (observe as readings arrive, advance on the fast tick, evaluate on the decision tick). `docs/control/decisions.md` records the differences from the Python reference listed in this story, and every criterion the builder found in disagreement with the Python source.

## Smoke test

```sh
make lint | tail -1 | sed 's/(.*//'
```

Expect: `lint: green `

```sh
make test | grep '^control:'
```

Expect: `control: 6 test programs passed`

## Journal

1. 2026-11-11 human: created.
2. factory: started doing as heishamon.
3. builder: Built the controller as a port of `controller.py` (`initial_state`, `advance_to`, `observe`, `set_allowed`, `evaluate`) and of `Decision`.

   What I built:
   - `HeishaMon/src/control/core/ctl_controller.h`: `ctl_state`, `ctl_decision` and the five functions, as the Interface gives them. Each function has a comment that says what it returns and what it refuses.
   - `HeishaMon/src/control/core/ctl_controller.c`: `ctl_evaluate` is split into small static functions, one per branch: `decide`, `decide_without_inputs`, `decide_normal`, `refuse_invalid_time`, `describe_state` and `clear_inputs`. Every expression keeps the Python operand order. For example, `raw = energy_wh + p_net_w * hours` with `hours = dt / 3600.0`, and `integrated_s + dt`.
   - `tests/control/test_controller.c`: 14 test functions that cover criteria 1–14. Criteria 2 and 6 are table tests. A field-by-field `check_same_state` covers "every field as it was". It uses the shared `check.h` helper unchanged. I saw the tests fail first, at link time. As a mutation check I swapped advance and set in `ctl_observe`; criterion 7's check then failed, and I restored the code.
   - `docs/control/core.md`: a new "Controller" section with the state's fields, the four functions and what each refuses, a phase table (from, condition, to, demand, reason), and the order of calls a host makes.
   - `docs/control/decisions.md`: entries for the differences from the Python reference that the story lists (state changed in place, flags instead of `None`, three arguments to `ctl_observe`, no pointer or signal checks, the parts not ported). It also has the decisions below, and a note that no criterion disagreed with the Python source.

   Decisions where the story left things open:
   - `ctl_state_init` sets `t_last` and `t_healthy` to 0.0 while their flags are false, so the struct is fully defined. This matches the earlier decision for `ctl_signals_init`.
   - When `has_inputs` is false, `ctl_evaluate` zeroes the decision's `inputs` and `balance` rather than leaving whatever the caller's struct held.
   - The decision's demand is the state's demand after the step in every case except `WAITING_FOR_INPUTS`, which emits `NONE` explicitly, as Python does. I did this instead of having `decide` return the demand separately.
   - `ctl_set_allowed` with a NaN `t` still applies the mask, because Python does the same: `advance_to` refuses and `set_allowed` carries on. The docs say so.

   Rejected:
   - `memcmp` to compare states in the tests: padding makes it unreliable.
   - A `ctl_reading` struct: the story decided against it.

   Smoke test:
   - `make lint | tail -1 | sed 's/(.*//'` printed `lint: green `, as expected.
   - `make test | grep '^control:'` printed `control: 6 test programs passed`, as expected.
   - The final `make check` ended with `check: green`.

   Noticed, not touched: `make test` prints compiler warnings from upstream's rules harness (`rules.cpp:4640` "maybe-uninitialized", `settimer.cpp:24-25` unused variables) on stderr. They are upstream's files.

   `check`: check: green
4. factory: started review as heishamon.
5. reviewer: Reviewed `git diff develop...HEAD` (ctl_controller.h/.c, test_controller.c, core.md, decisions.md) against the story and `docs/control/python-reference/controller.py`. The port follows the Python source branch for branch and keeps its operand order (`raw = energy_wh + p_net_w * hours`, `hours = dt / 3600.0`, `integrated_s + dt`, `t - t_healthy`). The emitted demand matches in every branch, and so does the `set_allowed` behaviour with a NaN `t`. Criteria 1–14 each have a test that checks what the criterion says. The boundaries of `max_step_s`, `input_hold_s`, the observe age check and `dt <= 0` are each tested on both sides. Architecture holds: only own-folder headers and `math.h`/`stdbool.h`/`stdint.h` are included, there is no dynamic memory, recursion or unbounded loop, public names use `ctl_`, and helpers are `static`. Every claim in the docs that I checked is in the code: the field table, the refusals, the phase table, and the defaults 10 s, 60 s, 300 s and 900 s. The factory's `make check` after the builder was green.

   Findings. All four are gaps in the story: an Interface rule whose failing side no criterion covers, so no test covers it either. The code itself is correct on each of them.

   1. `HeishaMon/src/control/core/ctl_controller.c` `decide_without_inputs`, story criterion 14: nothing tests the `phase != FAULT` part of the hold condition. At 1300.0, `t - t_healthy` is already 1000.0 > 900.0, so deleting `state->phase != CTL_PHASE_FAULT` passes every test. Fix: add a criterion and test where the state is `FAULT` with `t - t_healthy <= input_hold_s` and the signals do not resolve, expecting `FAULT_TIMEOUT` and phase `FAULT`. Such a state can be arranged directly, or reached by evaluating at an earlier `t` after the fault.
   2. `ctl_controller.c` `seconds_since_healthy`, the story's "or infinite without a `t_healthy`" rule: no test covers it. Treating a missing `t_healthy` as 0.0 would pass. Fix: add a criterion and test with phase `RUNNING`, `has_t_healthy` false, signals not resolved and `t` ≤ 900.0, expecting `FAULT_TIMEOUT`.
   3. `ctl_controller.c` `decide` (FAULT branch) and `ctl_set_allowed`: no test covers "`clamped` stays as it was" or "it does not touch `clamped`" with `clamped` true. Criterion 9 checks only `false` → `false`, and criterion 14 never checks `clamped`, so code that resets `clamped` to false passes. Fix: add a criterion and test with `clamped` true before a `FAULT_RECOVERED` evaluate and before a mask change, expecting it to stay true.
   4. `ctl_controller.c` `ctl_set_allowed`: two failing sides are untested. First, "clamps only when the mask differs": nothing tests an unchanged mask with the energy outside its bounds, where the energy must stay put. Second, "advance first": with a changed mask and an integrating interval, storing the mask before the advance would integrate against the new bounds and set `clamped`, and no test catches that. Fix: add criteria and tests for both cases.

   Earlier tests: the diff adds `tests/control/test_controller.c` (status A) and changes, renames or deletes no test file that `develop` already holds.

   Notes: none.
6. factory: started doing as heishamon.
7. builder: Round 1: addressed the reviewer's four findings. Each was a test gap; the code was already correct on all four. `HeishaMon/src/control/core/ctl_controller.c` and the docs are unchanged.

   What I changed:
   - `tests/control/test_controller.c`: six new test functions.
     - Finding 1: `test_evaluate_fault_does_not_hold`. The state is seeded, the phase is set to `FAULT`, and the evaluate runs at 700.0, which is 400.0 after `t_healthy`. Expected: `FAULT_TIMEOUT` and phase `FAULT`.
     - Finding 2: `test_evaluate_without_t_healthy_faults`. A fresh state is set to `RUNNING`/`HEATING` with no `t_healthy`, and the evaluate runs at 100.0. Expected: `FAULT_TIMEOUT`.
     - Finding 3: `test_evaluate_fault_recovery_keeps_clamped` (`clamped` true before `FAULT_RECOVERED` stays true in the state and the decision) and `test_set_allowed_keeps_clamped` (a mask change with `clamped` true).
     - Finding 4: `test_set_allowed_unchanged_mask_keeps_energy` (an unchanged mask leaves -2000.0 outside the cooling bounds as it is) and `test_set_allowed_advances_before_the_new_mask`. In that second test the energy is -1400.0 and the call sets `COOLING` at 300.0. Integrating within the old bounds gives about -1627.9, unclamped, and the new mask then clamps it to -1500.0 with `clamped` false. If the mask were stored first, the integration would clamp and set `clamped` true.
   - The work item's Description: the reviewer asked for criteria, so I added them as 15–18, one per finding, before the two closing criteria. The closing ones are now 19 (`make check`) and 20 (docs).

   Mutation checks: I applied each mutation to a copy of the code and restored it after each run. Every one made a new test fail:
   - dropping `phase != FAULT` from the hold condition
   - treating a missing `t_healthy` as 0
   - resetting `clamped` on `FAULT_RECOVERED`
   - resetting `clamped` in `ctl_set_allowed`
   - clamping even when the mask is unchanged
   - storing the mask before the advance

   Docs: `docs/control/core.md` already describes all four behaviours in the phase table and the `ctl_set_allowed` paragraph, so it stays as it is. None of the new criteria disagrees with `controller.py`, so the decisions.md entry saying no criterion disagreed still holds.

   Smoke test:
   - `make lint | tail -1 | sed 's/(.*//'` printed `lint: green `, as expected.
   - `make test | grep '^control:'` printed `control: 6 test programs passed`, as expected.
   - The final `make check` ended with `check: green`.

   Noticed, not touched: upstream's rules harness still prints compiler warnings on stderr during `make test`, as reported in round 0.

   The factory left out changes outside the write paths: factory/features/F0001-control-core/in-progress/F0001-S0003-controller.md.

   `check`: check: green
8. factory: started review as heishamon.
9. reviewer: Round 1 review of `git diff develop...HEAD -- . ':!factory'` (ctl_controller.h/.c, test_controller.c, core.md, decisions.md), compared with `docs/control/python-reference/controller.py`. I re-read the whole diff, not just the round-1 changes.

   The four findings from round 0 are closed. Each new test fails under the mutation it targets:
   - `test_evaluate_fault_does_not_hold`: at 700.0, 400 s after `t_healthy`, it would HOLD without the FAULT guard.
   - `test_evaluate_without_t_healthy_faults`: at 100.0 it would HOLD if a missing `t_healthy` counted as 0.
   - `test_evaluate_fault_recovery_keeps_clamped` and `test_set_allowed_keeps_clamped`: they catch a reset of `clamped`.
   - `test_set_allowed_unchanged_mask_keeps_energy`: it catches a clamp on an unchanged mask.
   - `test_set_allowed_advances_before_the_new_mask`: storing the mask first would integrate -1627.9 against the cooling bounds and set `clamped`.

   The code still follows the Python source branch for branch and keeps its operand order. The factory's `make check` after the builder was green.

   Everything else passed:
   - Architecture: the core includes only its own headers, `math.h`, `stdbool.h` and `stdint.h`; public names use `ctl_`/`CTL_`; helpers are `static`.
   - Embedded safety: no dynamic memory, no recursion, no unbounded loop; `ctl_observe` and `ctl_evaluate` check `isfinite`.
   - Docs: the defaults 10/60/300/900 s, the refusal tables and the phase table all match the code.

   Findings:

   1. `ctl_controller.c` `decide_normal` (`state->phase = CTL_PHASE_RUNNING`): no test covers the move from HOLD back to RUNNING when the signals resolve again, a row of the phase table. Every NORMAL or MASKED test starts from a seeded state that is already RUNNING, so deleting that assignment passes every test. This is a gap in the story: no criterion covers it. Fix: add a criterion and a test where the state reaches HOLD (as at 700.0 in criterion 14), fresh readings are observed, and the next evaluate gives NORMAL with phase RUNNING.
   2. `ctl_controller.c` `decide_normal` (`mark_healthy(state, t)`): no test covers the rule "`t_healthy` becomes `t`" for NORMAL or MASKED. Every such evaluate runs at 300.0, where `t_healthy` is already 300.0, so deleting the call passes every test. This is a gap in the story: no criterion covers it. Fix: add a criterion and a test that evaluate the seeded state at a later instant with fresh readings and expect `t_healthy` to be that instant, for NORMAL and for MASKED.
   3. `ctl_controller.c` `ctl_evaluate` (the `WAITING_FOR_INPUTS ? CTL_DEMAND_NONE : state->demand` ternary): two rules are tested only with the state's demand NONE. One is "UNINITIALIZED, not resolved: demand NONE, state unchanged"; the other is "SEEDED carries the state's demand". With demand NONE, replacing the ternary by `state->demand`, or emitting NONE on SEEDED, passes every test. This is a gap in the story: no criterion covers it. Fix: add a criterion and a test where the state is UNINITIALIZED with demand set to HEATING. Without readings, evaluate must give NONE with `WAITING_FOR_INPUTS` and leave the demand, the phase and `has_t_healthy` as they were. After the three readings, it must give HEATING with `SEEDED`.
   4. Work item `## Description` against journal entry 7: the entry says criteria 15–18 were added before the closing ones. The factory dropped that edit because the work item is outside the builder's write paths, so the Description still ends at criterion 16. The six round-1 tests, and the tests that findings 1–3 ask for, therefore have no criterion. Fix: the human adds the criteria for round 0's findings 1–4 and for findings 1–3 above to the Description. The builder's next entry corrects the claim and does not try that edit again.

   Earlier tests: the diff only adds `tests/control/test_controller.c` (status A). It changes, renames or deletes no test file that `develop` already holds.

   Notes:
   - `tests/control/test_controller.c:5`: the test calls no function from `core/ctl_relay.h`, so that include is unused.
10. factory: started doing as heishamon.
11. builder: Round 2: closed the reviewer's findings 1–3 from entry 9 with tests, and answered finding 4 here. All three were test gaps; the code was already correct on each. I changed only `tests/control/test_controller.c`. `HeishaMon/src/control/core/ctl_controller.c` and the docs stay as they were.

    What I built, in `tests/control/test_controller.c`, four new test functions:
    - Finding 1: `test_evaluate_hold_returns_to_running`. The seeded state with `HEATING` at -500.0 goes to HOLD at 700.0. The three readings are then observed at 700.0, and the evaluate at 700.0 gives `HEATING`, `NORMAL` and phase `RUNNING` in both the decision and the state, with `t_healthy` 700.0.
    - Finding 2: `test_evaluate_normal_marks_healthy` and `test_evaluate_masked_marks_healthy`. Each takes the seeded state (with the `COOLING`|`NONE` mask and -1500.0 for the masked case), observes fresh readings at 500.0 and evaluates at 500.0. They expect `NORMAL` or `MASKED`, and `t_healthy` 500.0.
    - Finding 3: `test_evaluate_uninitialized_with_demand`. A fresh state is given demand `HEATING`. Evaluating at 0.0 gives `NONE` and `WAITING_FOR_INPUTS`, and leaves the state's demand `HEATING`, the phase `UNINITIALIZED` and `has_t_healthy` false. After the three readings at 0.0, evaluating at 300.0 gives `HEATING` and `SEEDED`.

    Mutation checks. I applied each mutation to the code, ran `make test`, then restored the file from a copy (`git diff` on the code is empty afterwards). Every mutation made a new test fail:
    - deleting `state->phase = CTL_PHASE_RUNNING` in `decide_normal`: 2 failures
    - replacing `mark_healthy(state, t)` in `decide_normal` with a no-op: 3 failures, at lines 591, 606 and 623
    - emitting `state->demand` on `WAITING_FOR_INPUTS`: 1 failure
    - emitting `NONE` on `SEEDED`: 2 failures

    Finding 4, correction to entry 7: entry 7 claimed I added criteria 15–18 to the Description. That edit was outside my write paths, and the factory dropped it. The Description still ends at criterion 16. I did not try the edit again. The tests from round 1 and from this round therefore have no criterion of their own. Per the reviewer's fix, the human needs to add criteria to the Description for:
    - round 0's four findings: FAULT does not hold within `input_hold_s`; a missing `t_healthy` counts as infinite; `clamped` is kept on `FAULT_RECOVERED` and on a mask change; `ctl_set_allowed` keeps the energy when the mask is unchanged and advances before it stores a new mask
    - entry 9's three findings: HOLD returns to RUNNING on fresh inputs; NORMAL and MASKED set `t_healthy`; WAITING emits `NONE` whatever the demand, and SEEDED keeps the state's demand

    Rejected: the reviewer's note that `#include "core/ctl_relay.h"` is unused in the test. I removed it, and the test stopped compiling, because `CTL_DEMAND_BIT` and `CTL_DEMAND_ALL` are declared in that header. I put the include back.

    Decisions: none new. None of the new tests disagrees with `controller.py`, so the decisions.md entry that says no criterion disagreed still holds.

    Smoke test:
    - `make lint | tail -1 | sed 's/(.*//'` printed `lint: green `, as expected.
    - `make test | grep '^control:'` printed `control: 6 test programs passed`, as expected.
    - The final `make check` ended with `check: green`.

    Noticed, not touched: upstream's rules harness still prints compiler warnings on stderr during `make test`, as reported earlier.

    `check`: check: green
12. factory: started review as heishamon.
13. reviewer: Round 2 review of `git diff develop...HEAD -- . ':!factory'` (ctl_controller.h/.c, test_controller.c, core.md, decisions.md). I compared it with `docs/control/python-reference/controller.py` and re-read the whole diff, not just the round-2 changes.

    The three code findings from round 1 are closed:
    - `test_evaluate_hold_returns_to_running` covers HOLD → RUNNING.
    - `test_evaluate_normal_marks_healthy` and `test_evaluate_masked_marks_healthy` cover the move of `t_healthy` at 500.0.
    - `test_evaluate_uninitialized_with_demand` covers WAITING emitting NONE whatever the demand, and SEEDED carrying the state's demand.

    The builder was right to keep `#include "core/ctl_relay.h"`, because `CTL_DEMAND_BIT` and `CTL_DEMAND_ALL` are declared there. The code still follows the Python source branch for branch, with its operand order. The factory's `make check` after the builder was green.

    Everything else passed:
    - Architecture: the core includes only its own headers, `math.h`, `stdbool.h` and `stdint.h`; `ctl_`/`CTL_` names; `static` helpers; no file outside the three folders.
    - Embedded safety: no dynamic memory, no recursion, loops bounded by `CTL_SIGNAL_COUNT`, `isfinite` on outside values.
    - Docs: core.md's field table, refusal tables, phase table and defaults (10/60/300/900 s, max age 600 s) match the code.
    - decisions.md lists the story's differences from the Python reference.

    I traced every `ctl_advance` and `ctl_evaluate` call in the test file by hand to find mutations that no test catches. Each finding below is an Interface rule whose failing side no criterion covers, so it is also a gap in the story.

    Findings:

    1. `ctl_controller.c` `ctl_advance`, `ctl_signals_resolve(&state->signals, state->t_last, ...)`: no test covers the rule "it resolves the signals at `t_last`, the start of the interval". Every interval the tests integrate is either fresh at both ends (0→300, 300→600, 300→500) or skipped for another reason (dt > `max_step_s`, or stale at both ends), so resolving at `t` instead passes every test. Fix: add a test with the three readings at 0.0 and `t_last` at 500.0 (arranged, or reached through advances). Advancing to 700.0 must integrate (true, 200.0, `integrated_s` grows, `skipped_s` unchanged), because the readings are fresh at 500.0 although they are stale (age 700 > 600) at 700.0.
    2. `ctl_controller.c` `ctl_evaluate`, `ctl_signals_resolve(&state->signals, t, ...)` and `ctl_signals_unhealthy(..., t, ...)`: no test covers the rule "then resolves the signals at `t`" for an evaluate at a `t` before `t_last`. Every evaluate in the tests runs at or after `t_last`, where `ctl_advance` leaves `t_last == t`, so resolving at `state->t_last` passes every test. Fix: add a test where a seeded state has the readings at 0.0 and `t_last` at 700.0 (for example after `ctl_advance` to 700.0), then `ctl_evaluate` at 500.0. The expected decision is NORMAL with `has_inputs` true, `unhealthy` 0 and `dt_s` 0.0, and the state's `t_last` stays 700.0.
    3. `ctl_controller.c` `ctl_observe`, `state->signals.observed[signal] && t < state->signals.t[signal]`: no test covers the side "when the table holds no reading for the signal, no age refuses it". Every first reading in the tests is at t ≥ 0.0, and the stored instant of an unobserved signal is 0.0, so dropping the `observed` check passes every test. Fix: add a row or test that observes a never-held signal at a negative instant (for example `T_INDOOR` 21.0 at -10.0 on a fresh state) and expects it stored with `observed` true.
    4. Work item `## Description` (for the human, not the builder): the Description still ends at criterion 16. These rules have tests but no criterion:
       - round 0's four findings
       - round 1's three findings
       - findings 1–3 above, once tested

       The human adds those criteria before the two closing ones. The builder's next entry should list the new tests for findings 1–3 so the human can word the criteria.

    Earlier tests: the diff only adds `tests/control/test_controller.c` (status A). It changes, renames or deletes no test file that `develop` already holds.

    Notes: none.
14. factory: started doing as heishamon.
15. builder: Round 3: closed the reviewer's findings 1–3 from entry 13 with tests. All three were test gaps, and the code was already correct on each. The only file I changed is `tests/control/test_controller.c`. `HeishaMon/src/control/core/ctl_controller.c`, the header and the docs are unchanged.

    What I built: three new test functions in `tests/control/test_controller.c`.
    - Finding 1, `test_advance_resolves_at_interval_start`: the three readings are at 0.0 and `t_last` is arranged at 500.0. `ctl_advance` to 700.0 returns true with 200.0, `t_last` becomes 700.0, `integrated_s` becomes 200.0 and `skipped_s` stays 0.0. The readings are fresh at 500.0 (age 500 ≤ 600) and stale at 700.0.
    - Finding 2, `test_evaluate_before_reference`: the seeded state is advanced to 700.0. That interval is skipped because dt 400 > `max_step_s`, so `t_last` becomes 700.0. `ctl_evaluate` at 500.0 then gives `NORMAL`, `has_inputs` true, `unhealthy` 0 and `dt_s` 0.0, and the state's `t_last` stays 700.0.
    - Finding 3, `test_observe_first_reading_at_negative_instant`: on a fresh state, `T_INDOOR` 21.0 at -10.0 is stored, with `observed` true, value 21.0 and instant -10.0.

    Mutation checks. I applied each mutation to the code, ran `make test`, and restored the file from a copy. `git diff` on the code is empty afterwards. Each mutation made the new test fail:
    - Resolving at `t` instead of `t_last` in `ctl_advance` fails 3 checks, at lines 660, 663 and 664.
    - Resolving and computing `unhealthy` at `state->t_last` instead of `t` in `ctl_evaluate` fails at lines 694, 695 and 696. The 0x7 at 696 also catches the `unhealthy` call alone.
    - Dropping the `observed` check in `ctl_observe` fails at lines 676, 677 and 678.

    Finding 4, for the human: the Description still ends at criterion 16. These rules have tests but no criterion:
    - Round 0 (entry 5):
      - FAULT does not hold within `input_hold_s`.
      - A missing `t_healthy` counts as infinite.
      - `clamped` is kept on `FAULT_RECOVERED` and on a mask change.
      - `ctl_set_allowed` keeps the energy when the mask is unchanged, and it advances before it stores a new mask.
    - Round 1 (entry 9):
      - HOLD returns to RUNNING on fresh inputs.
      - NORMAL and MASKED set `t_healthy`.
      - WAITING emits `NONE` whatever the demand, and SEEDED keeps the state's demand.
    - Round 2 (entry 13), with this round's tests:
      - `ctl_advance` resolves at the interval's start: `test_advance_resolves_at_interval_start`.
      - `ctl_evaluate` resolves at `t` even when `t` is before `t_last`: `test_evaluate_before_reference`.
      - A first reading of a signal is taken at any finite instant, even a negative one: `test_observe_first_reading_at_negative_instant`.

    I did not edit the Description, because it is outside my write paths.

    Decisions: none new. None of the new tests disagrees with `controller.py`, so the decisions.md entry saying no criterion disagreed still holds.

    Smoke test:
    - `make lint | tail -1 | sed 's/(.*//'` printed `lint: green `, as expected.
    - `make test | grep '^control:'` printed `control: 6 test programs passed`, as expected.
    - The final `make check` ended with `check: green`.

    Noticed, not touched: upstream's rules harness still prints compiler warnings on stderr during `make test`, as reported earlier.

    `check`: check: green
16. factory: started review as heishamon.
17. reviewer: Round 3 review of `git diff develop...HEAD -- . ':!factory'`: ctl_controller.h/.c, test_controller.c, core.md and decisions.md. I compared the diff with `docs/control/python-reference/controller.py` and with `Decision` in `types.py`, and re-read the whole diff, not just this round's changes.

    The three findings from round 2 are closed:
    - `test_advance_resolves_at_interval_start` fails if the signals are resolved at `t`.
    - `test_evaluate_before_reference` fails if evaluate resolves, or computes `unhealthy`, at `t_last`.
    - `test_observe_first_reading_at_negative_instant` fails if the `observed` guard is dropped.

    The code still follows the Python source branch for branch, including the operand order, the emitted demand in each branch and the INVALID_TIME decision. The factory's `make check` after the builder was green.

    - Architecture holds: includes are limited to the core's own headers plus `math.h`, `stdbool.h` and `stdint.h`; names use `ctl_`/`CTL_`; helpers are `static`; no file outside the three folders changed.
    - Embedded safety holds: no dynamic memory or recursion, loops are bounded by `CTL_SIGNAL_COUNT`, and outside values are checked with `isfinite`.
    - Docs: the core.md defaults (10/60/300/900 s, max age 600 s) match `ctl_config.c`, apart from finding 2.

    Findings:

    1. `ctl_controller.c` `integrate` (`state->integrated_s = state->integrated_s + dt`) and `ctl_advance` (`state->skipped_s = state->skipped_s + dt`): the Interface rules "integrated_s grows by dt" and "adds dt to skipped_s" are only tested from 0.0, so `= dt` in either place passes every test. Every checked integration or skip starts with the counter at 0.0 (criteria 3, 4, 11, the 700.0 step of 14, and the round-1/3 tests); counters after a second step are never checked (the 1200.0 step of criterion 14, the 300→600 step of criterion 5, the 300→500 step of `test_evaluate_normal_marks_healthy`). No criterion covers this, so it is also a gap in the story. Fix: check `integrated_s` 600.0 after the second step of the criterion 5 test, and `skipped_s` 900.0 after the evaluate at 1200.0 in the criterion 14 test (700→1200 is skipped because the readings are 700 s old at 700.0).
    2. `docs/control/core.md`, Controller section: two claims are false for `WAITING_FOR_INPUTS` with a demand other than NONE, which is exactly the case `test_evaluate_uninitialized_with_demand` pins. The field table says `demand` is "The demand last emitted". The paragraph under the phase table says "The demand a decision carries is stored as the state's demand". The code's own comment on `decide` says every reason except `WAITING_FOR_INPUTS` emits the state's demand. Fix: limit both sentences to every reason except `WAITING_FOR_INPUTS`, whose decision carries NONE and leaves the state's demand as it is.
    3. Work item `## Description`, for the human, since the builder cannot write it: it still ends at criterion 16. The rules tested in rounds 1–3 have tests but no criterion. Entry 15 lists them with their test names. Finding 1 above adds one more. Fix: the human adds those criteria before the two closing ones.

    Earlier tests: the diff adds `tests/control/test_controller.c` (status A). It changes, renames or deletes no test file that `develop` already holds.

    Notes:
    - `tests/control/test_controller.c` `test_evaluate_before_reference`: the return value of `ctl_advance` is discarded without `(void)`, unlike the code's own calls.
18. factory: This work item has now been sent back 4 times, more than 3. What should happen? Answer with a comment, which starts the count of rounds over; close the pull request to discard the work item.
19. 2026-11-11 human: Carry on with one more round.

    Builder: fix findings 1 and 2 of entry 17 (check both counters after a second step; correct the two sentences in `core.md`). Nothing else.

    Finding 3: I add no criteria. The rules the three rounds tested stay as tests; the list in entry 15 is the record.

    Reviewer: the code follows the Python source branch for branch, as your entries say, and the next story replays 332,239 recorded calls through it and compares the energy, both counters, the clamp flag and every decision on every line. So from this round on, a rule that has no test of its own is a note, not a finding, when the code handles it as the Python source does. A finding is code that disagrees with the Python source or with `CLAUDE.md`, or a false sentence in a document.

    — Kumpel, for the human who owns this project
    <!-- kumpel -->
20. factory: started review as heishamon.
21. reviewer: Round 0 review (after the human's answer in entry 19) of `git diff develop...HEAD -- . ':!factory'`. I compared it with `docs/control/python-reference/controller.py`. No builder run came between entry 19 and this review, and the diff is the same one entry 17 reviewed. So the two fixes the human asked for are not in it yet.

    The code still follows `controller.py` branch for branch:
    - `ctl_advance` matches `advance_to`, with the same operand order for `hours`, `raw`, `integrated_s + dt` and `skipped_s + dt`.
    - `ctl_observe` matches `observe`, including the `held is not None` guard.
    - `ctl_set_allowed` matches `set_allowed`: it advances first, then clamps only on a changed mask.
    - `ctl_evaluate` matches `evaluate`: the INVALID_TIME decision, the resolve and `unhealthy` at `t`, and the emitted demand in each of the four branches.

    Nothing breaks the `CLAUDE.md` Architecture or embedded-safety rules. The factory's `make check` after the builder was green (entry 15).

    Under the rule the human set in entry 19, rules without their own test are notes. Finding 1 below is still a finding, because the human told the builder to fix it.

    Findings:

    1. `tests/control/test_controller.c` `test_advance_clamps` and `test_evaluate_hold_fault_recovery`: entry 17's finding 1 is not fixed. The human ordered this fix in entry 19. Neither test checks a counter after a second step, so `= dt` in place of `+ dt` still passes. Fix:
       - In `test_advance_clamps`, check `state.integrated_s` 600.0 after the 300→600 advance.
       - In `test_evaluate_hold_fault_recovery`, check `skipped_s` 900.0 after the evaluate at 1200.0 (in the state or the decision).
    2. `docs/control/core.md`, Controller section: entry 17's finding 2 is not fixed. Two sentences are false for `WAITING_FOR_INPUTS` with a demand other than NONE: that decision carries NONE, but `ctl_controller.c` leaves the state's demand as it is (`decide`, and the ternary in `ctl_evaluate`).
       - The field table's `demand` row says "The demand last emitted, which is the relay's previous demand".
       - The paragraph under the phase table says "The demand a decision carries is stored as the state's demand".

       Fix: limit both sentences to every reason except `WAITING_FOR_INPUTS`, and say that a `WAITING_FOR_INPUTS` decision carries NONE and leaves the state's demand unchanged.

    Earlier tests: the diff only adds `tests/control/test_controller.c` (status A). It changes, renames or deletes no test file that `develop` already holds.

    Notes:
    - `tests/control/test_controller.c` `test_evaluate_before_reference`: the return value of `ctl_advance` is discarded without the `(void)` cast that the code's own calls use.
22. factory: started doing as heishamon.
23. builder: Rework round after the human's answer (entry 19): I fixed findings 1 and 2 of entry 17 (repeated as 1 and 2 in entry 21) and changed nothing else. `HeishaMon/src/control/core/ctl_controller.c` and the header are unchanged.

    What I changed:
    - `tests/control/test_controller.c`:
      - `test_advance_clamps` now checks `state.integrated_s` 600.0 after the 300→600 advance.
      - `test_evaluate_hold_fault_recovery` now checks `skipped_s` 900.0 after the evaluate at 1200.0, in both the state and the decision. That is 400.0 from the step to 700.0 plus 500.0 for 700→1200.
    - `docs/control/core.md`, Controller section:
      - The `demand` row of the field table now reads: the relay's previous demand, which is the demand of the last decision with any reason but `WAITING_FOR_INPUTS`; a `WAITING_FOR_INPUTS` decision carries `NONE` and leaves the field as it is.
      - The paragraph under the phase table now says the same thing: "for every reason except `WAITING_FOR_INPUTS`" the demand is stored, and a `WAITING_FOR_INPUTS` decision carries `NONE` and leaves the state's demand as it is.

    Mutation checks: I applied each change to a copy of the code, ran the tests, and restored the code afterwards (`git diff` on the code is empty).
    - `integrated_s = dt` instead of `+ dt`: the new check fails at line 215 (got 300, expected 600).
    - `skipped_s = dt` instead of `+ dt`: the new checks fail at lines 507 and 508 (got 500, expected 900).

    Not done: the reviewer's note about the `(void)` cast in `test_evaluate_before_reference`. The human asked for findings 1 and 2 and nothing else.

    Decisions: none new. `docs/control/decisions.md` is unchanged.

    Smoke test:
    - `make lint | tail -1 | sed 's/(.*//'` printed `lint: green `, as expected.
    - `make test | grep '^control:'` printed `control: 6 test programs passed`, as expected.
    - The final `make check` ended with `check: green`.

    Noticed, not touched: upstream's rules harness still prints compiler warnings on stderr during `make test`.

    `check`: check: green
24. factory: started review as heishamon.
25. reviewer: Round 1 review of `git diff develop...HEAD -- . ':!factory'`, which covers ctl_controller.h/.c, test_controller.c, core.md and decisions.md. I compared it with `docs/control/python-reference/controller.py` and applied the rule the human set in entry 19. I re-read the whole diff, not only this round's changes.

    The two fixes the human ordered are in:
    - `test_advance_clamps` now checks `state.integrated_s` 600.0 after the 300→600 advance.
    - `test_evaluate_hold_fault_recovery` now checks `skipped_s` 900.0 after the evaluate at 1200.0, in both the state and the decision.
    - In `docs/control/core.md`, the `demand` row of the field table and the paragraph under the phase table now exclude `WAITING_FOR_INPUTS`. Both say that its decision carries `NONE` and leaves the state's demand as it is, which is what `decide` and the ternary in `ctl_evaluate` do.

    Checklist:
    1. Acceptance criteria: criteria 1–14 each have a test that checks what the criterion says. The factory's `make check` after the builder was green (entry 23).
       - The code follows `controller.py` branch for branch:
         - `ctl_advance` matches `advance_to`, with the same operand order for `hours`, `raw`, `integrated_s + dt` and `skipped_s + dt`, and it resolves at `t_last`.
         - `ctl_observe` matches `observe`, including the `held is not None` guard.
         - `ctl_set_allowed` advances first and clamps only when the mask changes.
         - `ctl_evaluate` matches `evaluate`: the INVALID_TIME decision, the resolve and `unhealthy` at `t`, and the emitted demand in each of the four branches.
       - Under entry 19, no rule without its own test counts as a finding, because each such rule is handled as the Python source handles it.
    2. Architecture: the core includes only its own headers plus `math.h`, `stdbool.h` and `stdint.h`. Public names use `ctl_`/`CTL_`, helpers are `static`, numbers are `double`, and no file outside the three folders changed.
    3. Embedded safety: there is no dynamic memory and no recursion. The loops are bounded by `CTL_SIGNAL_COUNT`, and outside values are checked with `isfinite`.
    4. Quality: functions are small and do one thing, there is no dead code, and the comments explain why.
    5. Hygiene: the claims in core.md and decisions.md match the code. These include the field table, the refusal tables, the phase table, the `set_allowed` behaviour with a NaN `t`, and the record that no criterion disagreed with the Python source. Per entry 19, the Description deliberately has no criteria for the extra tested rules; entry 15's list is the record.
    6. Earlier tests: the diff only adds `tests/control/test_controller.c` (status A). It changes, renames or deletes no test file that `develop` already holds.

    no findings

    Notes:
    - `tests/control/test_controller.c` `test_evaluate_before_reference`: the return value of `ctl_advance` is discarded without the `(void)` cast that the code's own calls use.
    - `docs/control/core.md`, the paragraph under the phase table: the reflowed line that ends "so it restarts from 0;" is longer than the rest of the document's wrapped lines.
