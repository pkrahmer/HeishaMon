# The control core

The control core is the part of the fork that decides, from five input signals, whether the
heat pump should heat, cool or do neither. It lives in `HeishaMon/src/control/core/` as plain
C99 that takes values and returns values: no input or output, no clock, no logger, no hidden
state. It is a port of the Python reference beside this document
(`docs/control/python-reference/`, described in `reference.md`), and the golden traces exported
from that reference are the measure the port is compared against, bit for bit. Where the port differs from the reference on purpose,
`decisions.md` says why.

## Enums (`ctl_types.h`)

Four enums carry the core's vocabulary. Each `ctl_*_name` function returns the name of the
Python enum member, which is the text a golden trace writes; a value that is no member,
`CTL_SIGNAL_COUNT` included, gives `"INVALID"`.

| Enum | Member | Name |
|---|---|---|
| `ctl_demand` | `CTL_DEMAND_HEATING` | `HEATING` |
| | `CTL_DEMAND_COOLING` | `COOLING` |
| | `CTL_DEMAND_NONE` | `NO_DEMAND` |
| `ctl_phase` | `CTL_PHASE_UNINITIALIZED` | `UNINITIALIZED` |
| | `CTL_PHASE_RUNNING` | `RUNNING` |
| | `CTL_PHASE_HOLD` | `HOLD` |
| | `CTL_PHASE_FAULT` | `FAULT` |
| `ctl_reason` | `CTL_REASON_NORMAL` | `NORMAL` |
| | `CTL_REASON_SEEDED` | `SEEDED` |
| | `CTL_REASON_WAITING_FOR_INPUTS` | `WAITING_FOR_INPUTS` |
| | `CTL_REASON_MASKED` | `MASKED` |
| | `CTL_REASON_HELD_STALE_INPUT` | `HELD_STALE_INPUT` |
| | `CTL_REASON_FAULT_TIMEOUT` | `FAULT_TIMEOUT` |
| | `CTL_REASON_FAULT_RECOVERED` | `FAULT_RECOVERED` |
| | `CTL_REASON_INVALID_TIME` | `INVALID_TIME` |
| `ctl_signal` | `CTL_SIGNAL_T_INDOOR` | `T_INDOOR` (required) |
| | `CTL_SIGNAL_T_OUTDOOR` | `T_OUTDOOR` (required) |
| | `CTL_SIGNAL_P_PRODUCED` | `P_PRODUCED` (required) |
| | `CTL_SIGNAL_P_SOLAR` | `P_SOLAR` (optional) |
| | `CTL_SIGNAL_P_INTERNAL` | `P_INTERNAL` (optional) |

`CTL_SIGNAL_COUNT` is no signal; it sizes the arrays indexed by signal.
`ctl_signal_is_required` is true for the three required signals and false for everything else.
Without a required signal the core can neither integrate nor decide; an optional signal that is
absent or stale is replaced by its fallback value.

## Configuration (`ctl_config.h`)

`ctl_config` holds every parameter the core reads. `ctl_config_defaults` fills it with the legacy
system's field-tuned values, which are valid. `ctl_config_validate` returns 0 for a valid
configuration and otherwise the bits of every bad field at once; the board calls it before it
applies a configuration. A field that is NaN or infinite is bad, except `INFINITY` in `max_age_s`
of an optional signal. `ctl_config_energy_limit_wh` returns
`energy_clamp_factor * hysteresis_wh`, 3000 Wh by default.

| Field | Meaning | Default | Rule |
|---|---|---|---|
| `desired_indoor_c` | The indoor temperature the controller holds, in °C. | 22.0 | 5.0 to 30.0, both included |
| `transmission_w_per_k` | Heat lost through the envelope and ventilation per kelvin of indoor–outdoor difference, in W/K. | 155.0 | > 0 |
| `adjustment_w_per_k` | Stiffness of the trim: watts of model error tolerated per kelvin of offset from the target; 0 disables the trim. | 805.0 | ≥ 0 |
| `hysteresis_wh` | Energy debt or surplus at which heating or cooling engages, in Wh; release is at 0. | 1500.0 | > 0 |
| `trim_limit_enabled` | Whether the deviation feeding the trim is clamped. | true | — |
| `trim_limit_k` | Clamp on the deviation feeding the trim, in K; bounds a stuck sensor. | 3.0 | > 0 when `trim_limit_enabled`, not checked otherwise |
| `solar_gain_factor` | Share of PV DC power that the adapter counts as solar gain into the house; the core never reads it. | 0.02 | ≥ 0 |
| `energy_clamp_factor` | Anti-windup bound on the energy accumulator, as a multiple of `hysteresis_wh`. | 2.0 | ≥ 1.0 |
| `max_step_s` | Longest interval, in s, still trusted for integration; longer gaps are skipped. | 300.0 | > 0 |
| `tick_interval_s` | The decision heartbeat, in s. | 60.0 | > 0 and ≤ `max_step_s / 2` |
| `integration_interval_s` | The integration-only heartbeat between decisions, in s. | 10.0 | > 0 and ≤ `tick_interval_s` |
| `input_hold_s` | How long, in s, the last demand is held across stale inputs before the output fails off. | 900.0 | ≥ 0 |
| `max_age_s[signal]` | How long, in s, a reading of the signal stays valid. | 600.0; `INFINITY` for `P_INTERNAL` | every entry > 0 and not NaN; finite for a required signal |
| `fallback_w[signal]` | Value, in W, that replaces an optional signal that is absent or stale. | 550.0 for `P_INTERNAL`, 0.0 otherwise | every entry finite |

| Problem bit | Value | Set when |
|---|---|---|
| `CTL_CONFIG_BAD_DESIRED_INDOOR` | `1u << 0` | `desired_indoor_c` breaks its rule |
| `CTL_CONFIG_BAD_TRANSMISSION` | `1u << 1` | `transmission_w_per_k` breaks its rule |
| `CTL_CONFIG_BAD_ADJUSTMENT` | `1u << 2` | `adjustment_w_per_k` breaks its rule |
| `CTL_CONFIG_BAD_HYSTERESIS` | `1u << 3` | `hysteresis_wh` breaks its rule |
| `CTL_CONFIG_BAD_TRIM_LIMIT` | `1u << 4` | `trim_limit_k` breaks its rule while the limit is enabled |
| `CTL_CONFIG_BAD_SOLAR_GAIN` | `1u << 5` | `solar_gain_factor` breaks its rule |
| `CTL_CONFIG_BAD_ENERGY_CLAMP` | `1u << 6` | `energy_clamp_factor` breaks its rule |
| `CTL_CONFIG_BAD_MAX_STEP` | `1u << 7` | `max_step_s` breaks its rule |
| `CTL_CONFIG_BAD_TICK_INTERVAL` | `1u << 8` | `tick_interval_s` breaks its rule |
| `CTL_CONFIG_BAD_INTEGRATION_INTERVAL` | `1u << 9` | `integration_interval_s` breaks its rule |
| `CTL_CONFIG_BAD_INPUT_HOLD` | `1u << 10` | `input_hold_s` breaks its rule |
| `CTL_CONFIG_BAD_MAX_AGE` | `1u << 11` | at least one entry of `max_age_s` breaks its rule |
| `CTL_CONFIG_BAD_FALLBACK` | `1u << 12` | at least one entry of `fallback_w` is not finite |

The three timing fields bound each other, and a bound is compared as the Python reference
compares it: `tick_interval_s` is refused when it is greater than `max_step_s / 2`, so a
`max_step_s` of 0 or below refuses the default tick as well, while a NaN `max_step_s` refuses
only itself. The same holds between `tick_interval_s` and `integration_interval_s`.

## Signal table (`ctl_signals.h`)

`ctl_signal_table` holds the last reading of each of the five signals: whether it was observed,
its value and its instant. A reading stays in force until the next reading of the same signal,
so all five signals are defined at every instant and the net power between two readings is a
constant. The caller owns the table; `ctl_signals_init` leaves no signal observed and
`ctl_signals_set` replaces one reading in place, checking nothing: the newest call wins, even
when its instant lies before the one it replaces.

The age of a signal at `at` is `at - t`, negative when `at` lies before the reading and
`INFINITY` for a signal never observed. A signal is **fresh** at `at` when it was observed and
`(at - t) <= max_age_s[signal]`: an age equal to the maximum is fresh, and a NaN `at` is never
fresh. Staleness is the negation of that test, `!(age <= max_age)`, and not `age > max_age`,
because the two differ for NaN. With the defaults, `P_INTERNAL` has a `max_age_s` of
`INFINITY`, so once observed it stays fresh at every instant that is not NaN; until its first
reading it falls back to `fallback_w` (550 W); at a NaN `at` it is not fresh, like every signal.

A set of signals is a bit mask, one `CTL_SIGNAL_BIT(signal)` per signal.
`ctl_signals_unhealthy` returns the required signals that are not fresh,
`ctl_signals_stale_optional` the optional ones that are not fresh, and `ctl_signals_observed`
every signal observed, fresh or not.

`ctl_signals_resolve` resolves the table to one instant. It refuses, returns false and leaves
`inputs` untouched when any required signal is not fresh at `at`, a NaN `at` included; the
caller must then neither integrate nor decide. Otherwise it returns true and fills `ctl_inputs`
with the held value of each required signal and of each fresh optional signal; an optional
signal that is not fresh is replaced by `fallback_w[signal]`, and its bit is set in
`substituted`.

## Energy balance (`ctl_balance.h`)

`ctl_energy_balance` treats the house as a thermal battery. Each formula keeps the Python
reference's operand order, sums from left to right:

| Term | Formula |
|---|---|
| `p_transmission_w` | `transmission_w_per_k * (t_outdoor_c - t_indoor_c)` |
| `p_model_w` | `p_produced_w + p_solar_w + p_internal_w + p_transmission_w` |
| `p_trim_w` | `adjustment_w_per_k * deviation`, with `deviation = t_indoor_c - desired_indoor_c` |
| `p_net_w` | `p_model_w + p_trim_w` |

`p_model_w` is the prediction and `p_trim_w` the correction for what the prediction gets wrong.
With `trim_limit_enabled`, the deviation, not the power, is clamped to `-trim_limit_k` below and
`trim_limit_k` above, so the limit stays in kelvin when `adjustment_w_per_k` is retuned; a
deviation equal to a limit is unchanged. A negative `p_net_w` is a deficit, a positive one a
surplus.

`ctl_ledger` integrates each term on its own, in Wh: `ctl_ledger_init` sets all five to 0, and
`ctl_ledger_accumulate` adds to each its power times `hours`: `p_produced_w`, `p_solar_w` and
`p_internal_w` from the inputs, `p_transmission_w` and `p_trim_w` from the balance.
`ctl_ledger_total_wh` returns `produced_wh + solar_wh + internal_wh + transmission_wh + trim_wh`,
summed from left to right.

## Relay and clamp (`ctl_relay.h`)

`ctl_relay` makes at most one transition from the demand last emitted, on the accumulated energy
`E` and the hysteresis `H`. Demand engages at `-H` or `H` and releases at 0, so heating never
turns straight into cooling, nor cooling into heating.

| Previous | Condition, tested in this order | Next |
|---|---|---|
| `HEATING` | `E >= 0.0` | `NONE` |
| | otherwise, NaN included | `HEATING` |
| `COOLING` | `E <= 0.0` | `NONE` |
| | otherwise, NaN included | `COOLING` |
| `NONE`, or no member | `E <= -H` | `HEATING` |
| | `E >= H` | `COOLING` |
| | otherwise, NaN included | `NONE` |

A set of demands is a bit mask, one `CTL_DEMAND_BIT(demand)` per demand, and `CTL_DEMAND_ALL`
holds all three. `ctl_clamp_bounds` gives the anti-windup bounds of the energy accumulator from
the allowed demands. The limit `L` is `ctl_config_energy_limit_wh`, 3000 Wh by default; a masked
direction clamps at exactly its engage threshold, so unmasking it gives an immediate but bounded
response. The bit of `NONE` plays no part.

| `allowed` has the bit of | Bound | Value |
|---|---|---|
| `HEATING` | `low` | `-L` |
| not `HEATING` | `low` | `-hysteresis_wh` |
| `COOLING` | `high` | `L` |
| not `COOLING` | `high` | `hysteresis_wh` |

`ctl_clamp` returns `low` when `value < low`, `high` when `value > high`, and `value`
otherwise, so a NaN passes through unchanged.

## Controller (`ctl_controller.h`)

The controller is the state the control loop remembers and four functions that change it in
place. Time handling is the algorithm here, so it is concentrated in `ctl_advance`: it is the
only function that moves the integration reference and the only one that changes the energy
integral; the other three call it first. Only `ctl_evaluate` changes the demand and the phase.

### The state

`ctl_state` is a struct the caller owns. `ctl_state_init` gives the state of a cold start.

| Field | Meaning | After `ctl_state_init` |
|---|---|---|
| `phase` | Where the controller is in its lifecycle; see the transitions below. | `UNINITIALIZED` |
| `energy_wh` | The accumulated energy deviation the relay acts on, in Wh: negative is a deficit, positive a surplus. Clamped on every integration step and every change of the mask. | 0.0 |
| `demand` | The relay's previous demand: the demand of the last decision with any reason but `WAITING_FOR_INPUTS`. A `WAITING_FOR_INPUTS` decision carries `NONE` and leaves this field as it is. | `NONE` |
| `allowed` | `CTL_DEMAND_BIT` of each permitted demand; the bit of `NONE` is always set by `ctl_set_allowed`. | `CTL_DEMAND_ALL` |
| `has_t_last`, `t_last` | The integration reference: the instant up to which the energy is integrated. | unset |
| `has_t_healthy`, `t_healthy` | The last instant at which `ctl_evaluate` found every required signal fresh; the hold is measured from it. | unset |
| `signals` | The signal table: the last reading of each signal. | empty |
| `integrated_s` | Seconds of elapsed time integrated, over the lifetime. Diagnostics only. | 0.0 |
| `skipped_s` | Seconds of elapsed time advanced over without integration, over the lifetime. Diagnostics only. | 0.0 |
| `ledger` | Each term of the balance integrated on its own, in Wh; it keeps accumulating past the clamp. | zero |
| `clamped` | Whether the last integration step hit the anti-windup bound. | false |

A value behind an unset flag is 0.0 and means nothing.

### The four functions

`ctl_advance(state, t, config, &dt_s)` moves the reference to `t` and returns whether it
integrated; `dt_s` gets the length of the interval it closed, or 0.0. An interval is integrated
only when it is trustworthy: no longer than `max_step_s` and with every required signal fresh
at its start, which zero-order hold says was true throughout it. Then `hours = dt / 3600.0`,
`raw = energy_wh + p_net_w * hours`, the energy becomes `raw` clamped to the bounds of
`allowed`, the ledger accumulates over `hours`, `integrated_s` grows by `dt`, and `clamped`
becomes whether the clamp changed `raw`. An interval that is not trustworthy adds no energy: the
reference still moves, and `skipped_s` grows by `dt`.

| `ctl_advance` refuses | and returns |
|---|---|
| a NaN or infinite `t`, changing nothing | false, 0.0 |
| a `t` at or before `t_last`, changing nothing | false, 0.0 |
| the first finite `t`, which only sets `t_last` | false, 0.0 |

`ctl_observe(state, signal, value, t, config)` takes one reading. It first advances to `t`, which
closes the open interval with the old value, and then holds the reading: a reading describes the
world from its instant onward and never before it. It refuses, changing nothing, a NaN or
infinite `value` or `t`, and a reading older than the one held for the same signal; one at the
same instant is taken. A reading before `t_last` but after its own signal's last reading is held
without integration and without moving `t_last`.

`ctl_set_allowed(state, allowed, t, config)` advances to `t`, always, and then permits the
demands of `allowed` and `NONE`. A mask that differs from the state's is stored and clamps the
energy to its new bounds at once, so that a masked direction cannot bank windup; it does not
touch `clamped`. It refuses nothing: with a NaN `t` the advance changes nothing, and the mask is
still applied.

`ctl_evaluate(state, t, config, &decision)` advances to `t`, resolves the signals at `t`, moves
the phase and the demand, and fills `ctl_decision`. The decision carries `dt_s` from the advance;
`has_inputs`, `inputs`, `balance` and `substituted` from the resolution, with `has_inputs` false
and `substituted` 0 when it refuses; `unhealthy` from `ctl_signals_unhealthy` at `t`; and the
phase, energy, counters, ledger and `clamped` of the state after the step. It refuses a NaN or
infinite `t`: the state stays as it is, and the decision carries the reason `INVALID_TIME`, the
state's demand and phase, `dt_s` 0.0, `has_inputs` false and both masks 0. A non-finite instant
is always a bug of the caller's clock.

### Phases

`resolved` below means that every required signal is fresh at `t`. The rows are tested in order.

| From | Condition | To | Demand | Reason |
|---|---|---|---|---|
| `UNINITIALIZED` | not resolved | `UNINITIALIZED` | `NONE` | `WAITING_FOR_INPUTS` |
| `UNINITIALIZED` | resolved; `t_healthy := t` | `RUNNING` | the state's | `SEEDED` |
| `FAULT` | resolved; `energy_wh := 0.0`, `t_healthy := t` | `RUNNING` | `NONE` | `FAULT_RECOVERED` |
| `RUNNING`, `HOLD` | not resolved, `t - t_healthy <= input_hold_s` | `HOLD` | the state's | `HELD_STALE_INPUT` |
| `RUNNING`, `HOLD`, `FAULT` | not resolved, otherwise | `FAULT` | `NONE` | `FAULT_TIMEOUT` |
| `RUNNING`, `HOLD` | resolved; `t_healthy := t` | `RUNNING` | `ctl_relay` | `NORMAL` |
| `RUNNING`, `HOLD` | resolved, the relay's demand not in `allowed`; `t_healthy := t` | `RUNNING` | `NONE` | `MASKED` |

Without a `t_healthy` the time since it is infinite. For every reason except
`WAITING_FOR_INPUTS`, the demand a decision carries is stored as the state's demand, the masked
one included, so that the relay's previous demand is always what was emitted. A
`WAITING_FOR_INPUTS` decision carries `NONE` and leaves the state's demand as it is. After a fault the accumulated energy is not trustworthy, so it restarts from 0;
expect up to one hysteresis of delay before the loop engages again.

### The order of calls

A host makes the calls on one monotonic clock, from one thread:

1. `ctl_state_init` once, at start.
2. `ctl_observe` as each reading arrives, at any rate, with the reading's sample instant, not its
   arrival instant.
3. `ctl_set_allowed` whenever the supervisor changes the permitted directions.
4. `ctl_advance` on the fast tick (`integration_interval_s`, 10 s): the integral is exact either
   way, but a finer grid takes a step in power into account sooner.
5. `ctl_evaluate` on the decision tick (`tick_interval_s`, 60 s), whether or not anything was
   observed: nothing else moves the demand, and a quiet system that is never evaluated never
   engages. Observe everything that has arrived before the evaluate of a tick.

A reading stamped far ahead of the others moves `t_last` to it and ages every other signal at
once; the host bounds timestamps as well as values.

## How the core is proven

A golden trace is the exact stream of calls the Python reference's simulator made into the Python
core, with the core's state written down after each call. The C core passes a trace when, started
cold and fed the same calls in the same order, it reproduces every recorded value;
`golden-traces.md` is the format and how the traces were made.

The runner is `tests/control/test_traces.c`, over the reader and replayer in
`tests/control/trace.c`. For each call line it calls `ctl_observe`, `ctl_advance`,
`ctl_set_allowed` or `ctl_evaluate`, then compares every recorded value: after the first three
the energy, the integrated and skipped seconds and the clamp flag of the state; after an evaluate
every field the trace records of the decision, from the demand to the five ledger terms. Numbers
are compared as doubles and exactly, with no tolerance: equal by `==` (so `-0.0` equals `0.0`)
or both NaN. A trace that does not follow the format is refused at the line of the first fault.

| Trace | Calls | What it is there for |
|---|---:|---|
| `barbarastr_march` | 129,493 | the house model under measured weather, 10–14 March 2023 (120 h), indoor thermometer silent for 60 min from 101.9 h: real PV, a real building, HOLD during a heating pulse |
| `cold_snap_undersized` | 12,952 | library scenario, 12 h: a cold snap on an undersized heat pump |
| `dhw_interruptions` | 14,033 | library scenario, 13 h: DHW diversion |
| `edge_cases` | 3,236 | scripted, what the simulator never produces: NaN and infinite values and instants, a clock going backwards, a duplicate instant, a stale duplicate reading, a reading older than the integration reference, a gap over `max_step_s`, mask changes that re-clamp E; the one trace that reaches every demand, reason and phase and both values of the clamp flag |
| `flaky_transport` | 7,403 | library scenario, 12 h: transport jitter, loss and out-of-order delivery |
| `indoor_sensor_outage` | 9,657 | library scenario, 9 h: a sensor outage |
| `masked_cooling` | 25,913 | library scenario, 24 h: a mask |
| `model_mismatch` | 12,952 | library scenario `model_mismatch_0.6`, 12 h: a house that differs from the model |
| `shoulder_season` | 25,912 | library scenario, 24 h: synthetic shoulder-season weather |
| `steady_winter` | 12,952 | library scenario, 12 h: synthetic steady winter weather |
| `summer_day` | 25,912 | library scenario, 24 h: a synthetic summer day |
| `underreported_solar` | 25,912 | library scenario `underreported_solar_0.15`, 24 h: solar reported at 15 % of true |
| `winter_day` | 25,912 | library scenario, 24 h: a synthetic winter day |

The library scenarios are, in the words of `golden-traces.md` section 2, there for the control law
under synthetic weather, transport jitter, loss, out-of-order delivery, DHW diversion, a sensor
outage and a mask; each keeps its own seed and is cut to 9–24 h.

`make test` unpacks the traces into a temporary folder, names it in `CTL_TRACES` and runs the
runner with the other test programs. When every trace passes, the runner prints

```
traces: 13 files, 332239 calls, 0 mismatches
```

the number of traces replayed, the call lines they held and the values that differed. It then
replays the small faulty traces under `tests/control/data/bad/`, which prove that the runner finds
what it should; the lines they print are expected. A value that differs prints one line with the
trace, the line number, the key, the expected and the actual value; each trace stops after ten.
The first such line is the one to read: later ones are mostly its consequences.

When a trace differs, the core is wrong: fix the core, never the trace, and add no tolerance.
When the Python core changes on purpose, the traces are exported again in the Python project and
replaced here as a whole, in the same change as the C core's port of that change.
