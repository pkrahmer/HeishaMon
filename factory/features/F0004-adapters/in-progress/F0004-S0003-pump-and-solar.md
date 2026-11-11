---
stage: approval
blocked: null
round: 1
attempts: 0
crashes: 0
pull_request: 14
comments_read: [6809683268]
handed_over: approval
claim: null
journal:
- number: 1
  speaker: human
  kind: created
  at: '2026-11-11T23:02:43.641713Z'
  stage: doing
- number: 2
  speaker: factory
  kind: started
  at: '2026-11-11T23:02:43.647717Z'
  stage: doing
- number: 3
  speaker: builder
  kind: result
  at: '2026-11-11T23:06:01.650405Z'
  stage: doing
  run: 545ec0c349af49eb973f633ce4d114bd
- number: 4
  speaker: factory
  kind: started
  at: '2026-11-11T23:06:07.431298Z'
  stage: review
- number: 5
  speaker: reviewer
  kind: result
  at: '2026-11-11T23:07:28.550692Z'
  stage: review
  run: 9662c72272934661b764a38e21e32ba7
- number: 6
  speaker: factory
  kind: started
  at: '2026-11-11T23:07:34.102214Z'
  stage: doing
- number: 7
  speaker: builder
  kind: result
  at: '2026-11-11T23:09:11.760942Z'
  stage: doing
  run: d5e65cf3ced94992aa2375b9fbf08829
- number: 8
  speaker: factory
  kind: started
  at: '2026-11-11T23:09:17.664389Z'
  stage: review
- number: 9
  speaker: reviewer
  kind: question
  at: '2026-11-11T23:10:00.685859Z'
  stage: review
  run: 7d556b862e36440dbfc81d6e9de40470
  blocked: question
- number: 10
  speaker: human
  kind: comment
  at: '2026-11-11T23:30:08.733193Z'
  stage: review
- number: 11
  speaker: factory
  kind: started
  at: '2026-11-11T23:30:11.359666Z'
  stage: review
- number: 12
  speaker: reviewer
  kind: result
  at: '2026-11-11T23:30:32.823599Z'
  stage: review
  run: 5032631f6ffb47f7b546e2308e49cf6d
---

# The pump signals and the solar gain

## Description

Three of the core's signals come from the heat pump and the PV system as MQTT payloads: HeishaMon publishes the outdoor temperature (`Outside_Temp`), the produced heat (`Heat_Power_Production`, in W), the position of the three-way valve (`ThreeWay_Valve_State`, 1 while the pump heats the hot-water tank) and the defrost flag (`Defrosting_State`); the PV system publishes its DC power. This story adds the adapter that turns them into readings for the core:

- the outdoor temperature as `T_OUTDOOR`;
- the produced heat as `P_PRODUCED`, reported as 0 W while the valve is on hot water or the pump defrosts, because that heat does not enter the rooms (`docs/control/algorithm-plan.md` [A-3]);
- the PV power times `solar_gain_factor` as `P_SOLAR`.

A change of the valve or the defrost flag emits the produced heat again under the new mask, so the core sees the switch when it happens, not at the next power report.

The module is pure: it takes payloads and instants, keeps the last values it needs, and returns at most one reading per call.

## Interface

New files: `HeishaMon/src/control/adapt/ctl_pump.h`, `ctl_pump.c`. Test file: `tests/control/test_pump.c`. It uses `ctl_payload`, `ctl_bounds` and the core's `ctl_signal`.

```c
typedef struct {
    ctl_signal signal;
    double value;
    double t;
} ctl_reading;

typedef struct {
    bool has_heat;
    double heat_w;   /* the last accepted produced heat, before the mask */
    bool hot_water;  /* the valve is on the hot-water tank */
    bool defrost;
} ctl_pump;

void ctl_pump_init(ctl_pump *pump);
bool ctl_pump_outdoor(const char *payload, size_t length, double t, ctl_reading *reading);
bool ctl_pump_heat(ctl_pump *pump, const char *payload, size_t length, double t, ctl_reading *reading);
bool ctl_pump_valve(ctl_pump *pump, const char *payload, size_t length, double t, ctl_reading *reading);
bool ctl_pump_defrost(ctl_pump *pump, const char *payload, size_t length, double t, ctl_reading *reading);
bool ctl_solar(const char *payload, size_t length, double gain, double t, ctl_reading *reading);
```

Each function returns true when it wrote a reading, and false when it wrote none. When it returns false it leaves `*reading` unchanged. A function refuses its input, and keeps its state as it was, when:

- the payload does not parse;
- the value is outside its bound;
- the value is not one of the values the function names;
- `t` is not finite.

- `ctl_pump_init`: no heat, valve not on hot water, no defrost.
- `ctl_pump_outdoor`: a value accepted by `CTL_BOUND_TEMPERATURE` gives `T_OUTDOOR` with that value at `t`.
- `ctl_pump_heat`: a value accepted by `CTL_BOUND_HEAT` is stored as the last heat and gives `P_PRODUCED` at `t`. The reading's value is 0.0 while `hot_water` or `defrost` is set, and the value itself otherwise.
- `ctl_pump_valve`: the value must be exactly 0.0 or 1.0. It sets `hot_water` to whether the value is 1.0. When that changes `hot_water` and a heat is stored, it gives `P_PRODUCED` at `t` under the new mask. A value that leaves `hot_water` as it was gives no reading, and neither does any value while no heat is stored.
- `ctl_pump_defrost`: the same as the valve, for `defrost`.
- `ctl_solar`: the PV power must be accepted by `CTL_BOUND_PV`, and `gain` must be finite and 0 or greater. It gives `P_SOLAR` with the value `pv * gain` at `t`.

## Acceptance criteria

1. `ctl_pump_outdoor`, as rows of one table test:
   - `5` at 100.0 gives `T_OUTDOOR` 5.0 at 100.0;
   - `-60` and `60` are accepted;
   - `-60.5`, `61` and `x` are refused;
   - a NaN `t` is refused;
   - a refused row leaves a marker-filled reading unchanged.
2. `ctl_pump_heat` without a mask:
   - `3200` gives `P_PRODUCED` 3200.0 and stores 3200.0;
   - `0` and `30000` are accepted;
   - `-1`, `30001` and `{"v":"x"}` are refused and keep the stored heat.
3. The mask by the valve, in order on one pump:
   - heat `3200` at 0.0 gives 3200.0;
   - valve `1` at 10.0 gives `P_PRODUCED` 0.0 at 10.0;
   - heat `3500` at 20.0 gives 0.0 and stores 3500.0;
   - valve `1` at 30.0 gives no reading;
   - valve `0` at 40.0 gives 3500.0 at 40.0;
   - valve `2` and valve `0.5` are refused.
4. The mask by defrost, in order on one pump:
   - heat `3200` at 0.0, then defrost `1` at 10.0, gives 0.0;
   - valve `1` at 20.0 gives no reading, because the reading is already masked and stays masked, while `hot_water` is now set;
   - defrost `0` at 30.0 gives no reading, because the valve still masks it, while `defrost` is now cleared;
   - valve `0` at 40.0 gives 3200.0.
5. Before any heat: valve `1` and defrost `1` give no readings but set their flags, and the first heat `3200` after them gives 0.0.
6. `ctl_solar`, as rows of one table test:
   - `1500` with the gain 0.02 gives `P_SOLAR` `1500.0 * 0.02` at `t`;
   - `0` gives 0.0;
   - `100000` is accepted and `100000.5` refused;
   - `-5` is refused;
   - a gain of -0.01, of NaN and of infinity is refused;
   - a gain of 0.0 gives 0.0.
7. `make check` stays green.
8. docs: `docs/control/adapters.md` gets a section on the pump signals and the solar gain. It has a table of the five functions with their payload, their bound and the reading they give, and the mask rule with the sequence of criterion 4 as its example. It says why DC and not AC PV power, in one sentence with a pointer to `docs/control/algorithm-plan.md` §1.9. `docs/control/decisions.md` records that defrost masks like hot water, and that a negative value is dropped and not clamped.

## Smoke test

```sh
make test | grep '^control:'
```

Expect: a line `control: N test programs passed`, where N is one more than before this story

## Journal

1. 2026-11-11 human: created.
2. factory: started doing as heishamon.
3. builder: Built the pump and solar adapter, with its test and documents.

   - `HeishaMon/src/control/adapt/ctl_pump.h`, `ctl_pump.c`: `ctl_reading`, `ctl_pump` and the six functions from the Interface. The code is pure C99. It uses `ctl_payload_parse`, `ctl_bounds_accept` and `ctl_signal` (included from `../core/ctl_types.h`). A refused input leaves both `*reading` and the pump's state as they were.
   - `tests/control/test_pump.c`: table tests for outdoor (criterion 1) and solar (criterion 6), and step sequences for heat without a mask (2), the valve mask (3), the defrost mask (4) and flags set before any heat (5). Each refused row checks that a reading filled with marker values stays unchanged, and that the pump's state stays the same too. I also added rows for a non-finite `t` on every function and for the payloads `x` and `-1` on the flags. I watched the test fail first: it did not compile while the header was missing.
   - `docs/control/adapters.md`: a new section with the table of the five functions (payload, bound or values, reading), the mask rule with criterion 4's sequence as its example table, and the DC-not-AC sentence pointing to `algorithm-plan.md` §1.9.
   - `docs/control/decisions.md`: three entries. Defrost masks the produced heat as hot water does. A negative value is refused, not clamped. The flags accept exactly 0 and 1.

   Decisions where the story was open:
   - **When a flag gives a reading.** A valve or defrost change gives a reading when the mask as a whole (`hot_water || defrost`) switches on or off, and a heat is stored. I compare the mask's state, not the masked value. A switch therefore gives a 0.0 reading even when the stored heat is 0 W, which matches the Description's "gives P_PRODUCED under the new mask". `test_zero_heat_switch` covers this case. I rejected comparing the masked values, because that would stay silent on a switch while the stored heat is 0 W.
   - `ctl_pump_init` sets `heat_w` to 0.0, so the state is defined throughout.
   - `ctl_solar` checks the gain before it parses the payload. Both are refused anyway, so the order changes nothing a caller sees.

   Smoke test: `make test | grep '^control:'` prints `control: 13 test programs passed`. There were 12 test programs before, so this meets the Expect line. The final `make check` ended with `check: green` (lint, test and device all green).

   Noticed but not touched: `ctl_reading` is declared in `ctl_pump.h`. A later adapter that also returns readings may want it in a shared header.

   `check`: check: green
4. factory: started review as heishamon.
5. reviewer: Reviewed `git diff develop...HEAD` (ctl_pump.h/.c, test_pump.c, adapters.md, decisions.md) against the work item, and checked the claims against ctl_bounds.h, ctl_payload.h, ctl_types.h, ctl_config.c and algorithm-plan.md §1.9 and [A-3].

   Findings:

   1. `factory/features/F0004-adapters/in-progress/F0004-S0003-pump-and-solar.md` `## Description`, Interface bullet `ctl_pump_valve` (and `ctl_pump_defrost` "the same"): the bullet says a reading comes "when that changes `hot_water` and a heat is stored". The code (`ctl_pump.c` `set_mask_flag`) gives a reading only when the whole mask `hot_water || defrost` switches. That matches criterion 4, but the Description was never updated (R5). Fix: rewrite both bullets to say "when that switches the mask (`hot_water` or `defrost`) on or off and a heat is stored, including a stored heat of 0 W".
   2. `HeishaMon/src/control/adapt/ctl_pump.c:26-28`: the `parse_bounded` comment says "A negative value is refused, not clamped to 0". But the same function serves `CTL_BOUND_TEMPERATURE`, which accepts values down to -60, so the comment is wrong for one of its three callers. Fix: say that a negative heat or PV power is refused because its bound starts at 0, or move the reason to the heat and solar callers.
   3. `HeishaMon/src/control/adapt/ctl_pump.h:48`: the `ctl_pump_valve` comment has a sentence spliced in ("and a change while defrost still masks, change nothing the core sees"). The line is 122 columns, while the rest of the adapter sources stay within 100. Fix: rewrap the comment and make the sentence read as one.
   4. `tests/control/test_pump.c` `test_flags_before_heat`: `pump.heat_w = 0.0;` right after `ctl_pump_init` is dead code. Its comment ("heat_w is not stated before the first heat") contradicts `ctl_pump.h`, and the decisions.md entry "The pump's flags accept exactly 0 and 1", which both say init sets `heat_w` to 0.0. Meanwhile `test_init` never checks `heat_w`, so the promised behaviour has no test. Fix: delete the override and its comment, and add `CHECK_DOUBLE(pump.heat_w, 0.0)` to `test_init`.
   5. `HeishaMon/src/control/adapt/ctl_pump.c` `ctl_solar`: the reading's value `pv_w * gain` is never checked. A finite gain above about 1.8e303, which both the Interface and `ctl_config.c` (`is_at_least(solar_gain_factor, 0.0)`) accept, makes an infinite `P_SOLAR` from an accepted PV power. CLAUDE.md requires a value from outside to be checked with `isfinite` before it is used. Fix: refuse when the product is not finite, and add a test row (for example `"100000"` with gain `1e304`). Or, if the builder sees this as beyond the story, end with `question` to the human. This is a gap in the story too: the Interface states no rule for the product.

   Checked and fine:
   - Every criterion 1-6 has a test that does what the criterion says, with boundary rows on both sides: -60/60 against -60.5/61, 0/30000 against -1/30001, 100000 against 100000.5, gain 0.0 against -0.01.
   - Every function has a row for a non-finite `t`. Refused rows check the marker reading and the pump state.
   - The journal records `make check` green.
   - Architecture holds: the code is pure C99 with allowed includes and relative includes between library folders, uses no dynamic memory, loops or recursion, keeps helpers `static`, and every public name carries the `ctl_` prefix. No file outside the three folders changed.
   - The docs criterion is met: adapters.md has the five-function table, the mask rule with criterion 4's sequence, and a DC-not-AC sentence that matches §1.9. decisions.md has the defrost entry and the negative-value entry. Their claims match the code, apart from finding 2's comment.

   Earlier tests: none. `git diff --name-status develop...HEAD` shows only `A tests/control/test_pump.c` under tests.
6. factory: started doing as heishamon.
7. builder: Round 1: fixed the reviewer's five findings and nothing else.

   1. Description: I rewrote the `ctl_pump_valve` bullet, which `ctl_pump_defrost` refers to as "the same". A reading now comes "when that switches the mask (`hot_water` or `defrost`) on or off and a heat is stored, including a stored heat of 0 W". A value that leaves the mask as it was gives none. Because of finding 5, I also added to the `ctl_solar` bullet that it refuses a product that is not finite.
   2. `HeishaMon/src/control/adapt/ctl_pump.c`: I took the "negative value is refused, not clamped" comment off `parse_bounded`, since that function also serves the temperature. The reason now sits at the two callers it applies to: `ctl_pump_heat` (CTL_BOUND_HEAT starts at 0 W) and `ctl_solar` (CTL_BOUND_PV starts at 0 W).
   3. `HeishaMon/src/control/adapt/ctl_pump.h`: I rewrapped the `ctl_pump_valve` comment into whole sentences. Every line in `ctl_pump.h` and `ctl_pump.c` is now 100 columns or less.
   4. `tests/control/test_pump.c`: I deleted the dead `pump.heat_w = 0.0;` override and its comment from `test_flags_before_heat`. `test_init` now checks `CHECK_DOUBLE(pump.heat_w, 0.0)`.
   5. `ctl_solar` now computes `pv_w * gain` and refuses the reading when the product is not finite. I added the test row `{"100000", 1e304, ...}`, which is refused. The header comment, the `ctl_solar` row of the table in `docs/control/adapters.md` ("the product finite") and a new entry in `docs/control/decisions.md` ("The solar gain refuses a product that is not finite") all say this now. I decided this myself rather than asking, because CLAUDE.md already requires an `isfinite` check on a value from outside. I rejected an upper bound on the gain inside the adapter: it would be a second rule beside the configuration's, and a guess at the largest sensible gain.

   The new row was added after the fix, so I did not see it fail. Without the check, 100000 × 1e304 is infinite and would be written as a reading, so the row would fail.

   Smoke test: `make test | grep '^control:'` prints `control: 13 test programs passed`, one more than the 12 before this story. Final `make check`: `check: green` (lint, test and device all green).

   Still noticed but not touched: `ctl_reading` lives in `ctl_pump.h`. A later adapter that also gives readings may want it in a shared header.

   The factory left out changes outside the write paths: factory/features/F0004-adapters/in-progress/F0004-S0003-pump-and-solar.md.

   `check`: check: green
8. factory: started review as heishamon.
9. reviewer: Round 1 review of `git diff develop...HEAD -- . ':!factory'` (ctl_pump.h/.c, test_pump.c, adapters.md, decisions.md) against the work item. I checked it against ctl_bounds.h/.c, ctl_payload.h, ctl_types.h, decode.h (TOP20, TOP26) and algorithm-plan.md §1.9 and [A-3]. The builder fixed all five findings from round 0 in the code, the tests and the documents. One finding is left, and the builder cannot fix it.

   Findings:

   1. `factory/features/F0004-adapters/in-progress/F0004-S0003-pump-and-solar.md` `## Description` → Interface: the Description still does not match what was built. The `ctl_pump_valve` bullet still says a reading comes "when that changes `hot_water` and a heat is stored". That contradicts criterion 4 (valve `1` at 20.0 changes `hot_water` while a heat is stored, and gives no reading) and the code (`ctl_pump.c` `set_mask_flag` gives a reading only when the whole mask switches). The `ctl_solar` bullet also leaves out the refusal of a product that is not finite, which the code, the header, adapters.md and decisions.md all state. The builder did rewrite both bullets in round 1, but journal entry 7 records that the factory dropped that edit. In `factory/pipeline.yml` the builder's write paths do not include `{work_item}`, so sending the item back to `doing` would only repeat the round.

   Question for the human: how should the two Interface bullets be corrected?
   - (a) You edit the Description yourself on the branch. Change the `ctl_pump_valve` bullet to "When that switches the mask (`hot_water` or `defrost`) on or off and a heat is stored, including a stored heat of 0 W, it gives `P_PRODUCED` at `t` under the new mask; a value that leaves the mask as it was gives no reading". Add to the `ctl_solar` bullet: "and the product `pv * gain` must be finite".
   - (b) You instruct a stage that may write the work item (the reviewer has `{work_item}` in its write paths) to make exactly that edit.
   - (c) You accept the Description as it is and let criterion 4 and the code stand as the truth.
   I recommend (a). Once the Description matches, I see nothing else that blocks approval.

   Checked and fine:
   - **Criteria:** criteria 1–6 each have a test that does what the criterion says.
     - Boundaries are tested on both sides: -60/60 against -60.5/61, 0/30000 against -1/30001, 100000 against 100000.5, gain 0.0 against -0.01.
     - Every function has a row with a non-finite `t`, and refused rows check the marker reading and the pump state.
     - The new `1e304` row covers the finite-product rule.
     - `test_init` now checks `heat_w`.
     - The journal records `make check` green.
   - **Architecture:** pure C99 with only allowed includes, relative includes across library folders, and no dynamic memory, recursion or loops in the adapter. Every name has the `ctl_` prefix and helpers are `static`. No file changed outside the three folders.
   - **Safety:** every outside double is checked: the bound check uses `isfinite`, flags must equal exactly 0.0 or 1.0 (so NaN is refused), and the gain, the product and `t` are each checked with `isfinite`.
   - **Quality:** the `parse_bounded` comment no longer claims anything about negative values. The `ctl_pump.h` comments read as whole sentences, and `ctl_pump.h`/`ctl_pump.c` stay within 100 columns.
   - **Docs:** adapters.md has the five-function table, the mask rule with criterion 4's sequence, and a DC-not-AC sentence that matches algorithm-plan.md §1.9 (lines 320–324). decisions.md records that defrost masks like hot water, that a negative value is refused and not clamped, that flags accept exactly 0 and 1, and that a non-finite product is refused. All of these match the code.

   Earlier tests: none. The only change under tests is `A tests/control/test_pump.c`.

   Notes:
   - `tests/control/test_pump.c:50`: the `feed` signature is longer than 100 columns.
   - `tests/control/test_pump.c:265`: in `test_solar`, the `ctl_solar` call line is longer than 100 columns.
10. 2026-11-11 human: The Description is corrected by this answer, which the reviewer reads as part of it:

    - `ctl_pump_valve` (and `ctl_pump_defrost` alike): when the new value switches the mask (`hot_water` or `defrost`) on or off and a heat is stored, including a stored heat of 0 W, it gives `P_PRODUCED` at `t` under the new mask; a value that leaves the mask as it was gives no reading.
    - `ctl_solar`: the product `pv * gain` must also be finite.

    The code and criterion 4 were right; the bullet was mine and wrong. No change to code or tests is needed. Reviewer: the Description now matches; with no other finding, end with approval.

    — Kumpel, for the human who owns this project
    <!-- kumpel -->
11. factory: started review as heishamon.
12. reviewer: no findings

    Round 1 review, run again after the human's answer (entry 10). I reviewed the current `git diff develop...HEAD -- . ':!factory'`: `ctl_pump.h`, `ctl_pump.c`, `test_pump.c`, `adapters.md` and `decisions.md`. I read the work item's Description together with entry 10's correction, as the answer asks.

    - **Acceptance criteria:**
      - Criteria 1–6 each have a test that does what the criterion says:
        - `test_outdoor` covers criterion 1.
        - `test_heat_unmasked` covers criterion 2.
        - `test_valve_mask` covers criterion 3, including valve `2` and `0.5`.
        - `test_defrost_mask` follows criterion 4 step for step, with the flags checked after each step.
        - `test_flags_before_heat` covers criterion 5.
        - `test_solar` covers criterion 6.
      - Each limit is tested on both sides: -60/60 against -60.5/61, 0/30000 against -1/30001, 100000 against 100000.5, and the gain 0.0 against -0.01, NaN and infinity.
      - Entry 10 adds the rule that the product must be finite. The `{"100000", 1e304}` row tests the side where it fails.
      - Every function has a row with a non-finite `t`. Each refused row checks that the marker reading is unchanged, and the sequence rows also check the pump's state.
      - The journal records `check: green` after the builder.
    - **Architecture:**
      - The code is pure C99. Its includes are the allowed ones, and it reaches the core's header by a relative path.
      - Every public name starts with `ctl_`, and the helpers are `static`.
      - No file outside the three folders changed.
    - **Embedded safety:**
      - There is no dynamic memory, no recursion and no loop in the adapter.
      - Every double from outside is checked before use:
        - `ctl_bounds_accept` checks the bounded values.
        - The flags must equal exactly 0.0 or 1.0, so a NaN is refused.
        - The gain, the product and `t` are each checked with `isfinite`.
    - **Quality:**
      - The comments give reasons, and the reason for refusing a negative value sits at the heat and solar callers it applies to.
      - There is no dead code, and no helper is copied.
    - **Work item hygiene:**
      - With entry 10, the Interface matches the code. A reading comes when the mask switches and a heat is stored, including a stored heat of 0 W. A product that is not finite is refused.
      - The journal explains the main decision (a reading on a switch of the mask, not on a change of the masked value) and what was rejected.
      - `adapters.md` has:
        - the table of the five functions
        - the mask rule, with criterion 4's sequence as its example
        - the sentence on DC, not AC, with its pointer to §1.9
      - `decisions.md` records that defrost masks like hot water, that a negative value is refused and not clamped, that the flags accept exactly 0 and 1, and that a product that is not finite is refused. All of this matches the code.
    - **Earlier tests:** none. The only change under tests is `A tests/control/test_pump.c`.

    Notes:
    - `tests/control/test_pump.c:50`: the `feed` signature is longer than 100 columns.
    - `tests/control/test_pump.c:265`: the `ctl_solar` call in `test_solar` is longer than 100 columns.
