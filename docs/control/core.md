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
reading, and at a NaN `at`, it is not fresh and falls back to `fallback_w` (550 W).

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
