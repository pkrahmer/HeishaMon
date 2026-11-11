---
stage: review
blocked: null
round: 0
attempts: 0
crashes: 0
pull_request: 14
comments_read: []
handed_over: null
claim:
  instance: heishamon
  stage: review
  started_at: '2026-11-11T23:06:07.431298Z'
  lease_duration: 60
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
