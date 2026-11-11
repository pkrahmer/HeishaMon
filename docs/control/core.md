# The control core

The control core is the part of the fork that decides, from five measured signals, whether the
heat pump should heat, cool or do neither. It lives in `HeishaMon/src/control/core/` as plain
C99 that takes values and returns values: no input or output, no clock, no logger, no hidden
state. It is a port of the Python reference beside this document
(`docs/control/python-reference/`, described in `reference.md`), and the golden traces exported
from that reference check it bit for bit. Where the port differs from the reference on purpose,
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
