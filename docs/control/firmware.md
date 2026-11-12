# The firmware

The firmware's own code around the loop: what the board keeps, reads and publishes. It lives in
`HeishaMon/src/control/glue/`; the parts there that are `.c` files are pure like the loop and are
tested on the host.

## The settings

Everything the owner can set is one flat set of names, each with a text value
(`glue/ctl_settings.h`). The settings file, the MQTT setting topics and a later web page all go
through the same three functions:

- `ctl_settings_set` parses and checks one value, then checks the whole set, and applies the value
  only when both pass.
- `ctl_settings_get` writes any value back as text, in the form the setter takes.
- `ctl_settings_to_loop` turns the settings into the loop's configuration (`loop.md`).

`ctl_settings_name_count` and `ctl_settings_name_at` list the names, in the order of the table
below, with the rooms' names after `rooms`.

| Name | Value text | Default | Rule |
|---|---|---|---|
| `enabled` | `0` or `1` | `0` | |
| `cooling` | `0` or `1` | `0` | |
| `rooms` | decimal | `0` | 0 to 16; a lower count keeps the topics and weights above it unlisted |
| `room.<i>.topic` | text | empty | one per room `i` below `rooms`; see topics below |
| `room.<i>.weight` | number | `1` | one per room `i` below `rooms`; 0 to 1000 |
| `room_timeout_s` | number | `1800` | greater than 0 |
| `pv_topic` | text | empty | empty means no PV; see topics below |
| `desired_indoor_c` | number | the core's default | as the core validates it |
| `transmission_w_per_k` | number | the core's default | as the core validates it |
| `adjustment_w_per_k` | number | the core's default | as the core validates it |
| `hysteresis_wh` | number | the core's default | as the core validates it |
| `trim_limit_k` | number or `none` | `3` | `none` disables the trim limit |
| `solar_gain_factor` | number | the core's default | as the core validates it |
| `internal_w` | number | `550` | the core's `fallback_w` of `P_INTERNAL`; finite |
| `heat_cool_heat_value` | `-1`, `0` or `1` | `-1` | |
| `min_change_interval_s` | number | `300` | as the router validates it |
| `retry_interval_s` | number | `120` | as the router validates it |
| `max_retries` | decimal | `3` | as the router validates it |

The values:

- A number is a bare number as the payload parser reads it (`adapters.md`), never a JSON object.
  It must have a magnitude below 10¹² and no more than three decimals, so that the getter writes
  it back exactly: a value saved through the getter reads back as the same value. `trim_limit_k`
  must also be greater than 0.
- A decimal (`rooms`, `max_retries`, the flags and `heat_cool_heat_value`) is a number that is
  whole and in its range.
- A topic is empty, or 1 to 96 printable ASCII characters (0x21 to 0x7e) without the wildcards
  `+` and `#`. An empty room topic is a room that never receives a value.
- The getter writes a whole number without decimals and any other number with its decimals, the
  trailing zeros cut: `1500`, `0.02`, `22.5`.

**A refused change keeps the old value.** The setter answers, in this order: `UNKNOWN` for a name
that is not listed (a room at or above `rooms` included), `BAD_VALUE` for a text that does not
parse or breaks its own row's rule, and `INVALID_SET` when the value is fine alone but the loop's
configuration made from the whole set would fail `ctl_loop_config_valid`, such as every room
weight at 0. The value is applied to a copy first, so every answer but `OK` leaves the settings as
they were: valid means apply, invalid means reject and keep the old value
(`firmware-design.md` §6). The setter checks each row's own rule, as the table gives it: the flags,
the ranges of `rooms` and `heat_cool_heat_value`, the room weight, `room_timeout_s` above 0,
`trim_limit_k` above 0, and the topics. Some of these the rooms check again; every rule the table
leaves to the core, the rooms or the router is checked only there.

**`rooms` 0 is valid.** A board fresh from the flash has no room topics yet, and its settings must
still be a valid set, or the owner could not save the first one. With no rooms, the loop gets one
room of weight 1 that never receives a value: the core waits for its inputs (`WAITING_FOR_INPUTS`),
and the glue publishes that, rather than refusing to start.

The loop's configuration starts from `ctl_loop_config_defaults` and takes from the settings the
core's tuning, with `internal_w` as the fallback of `P_INTERNAL`; the rooms' count, weights and
timeout; the router's pacing and polarity; the allowed demands, heating and no demand, with
cooling when `cooling` is 1; and no assumed landing.

## The telemetry

The board publishes every input, status, output and setting (`firmware-design.md` §6, decision
F-7), so that the owner can see what the loop saw, what it decided and why, and what it sent. The
pure part (`glue/ctl_telemetry.h`) lists, for a decision record, the rooms and the settings, each
subtopic with its value text; the glue prefixes `<mqtt_topic_base>/control/` and publishes them.
The loop never reads them back.

The entries come one at a time, by index, so that the glue needs no buffer for all of them:
`ctl_telemetry_count` gives a group's count and `ctl_telemetry_entry` the entry at an index of
`CTL_GROUP_INPUTS`, `CTL_GROUP_STATUS` or `CTL_GROUP_OUTPUTS`. The source holds the record of the
last decision tick (`loop.md`), the rooms and their configuration, the uptime in seconds and
whether commands are enabled. A subtopic, with its terminating zero, fits
`CTL_TELEMETRY_SUBTOPIC_MAX` (48 bytes).

A number is written by `ctl_format_fixed` with the decimals the tables name; a value the record
does not have is written `-`. A flag is `1` or `0`.

**Inputs**, 9 entries and 3 for each configured room:

| Subtopic | Value |
|---|---|
| `inputs/indoor_c` | the decision's indoor input, 2 decimals |
| `inputs/outdoor_c` | outdoor input, 2 decimals |
| `inputs/produced_w` | produced input, 0 decimals |
| `inputs/solar_w` | solar input, 0 decimals |
| `inputs/internal_w` | internal input, 0 decimals |
| `inputs/substituted` | the substituted signals' names joined by `,`, such as `P_SOLAR,P_INTERNAL`, or `-` |
| `inputs/unhealthy` | the unhealthy signals' names joined by `,`, or `-` |
| `inputs/rooms_fresh` | the fresh rooms' count |
| `inputs/rooms_fresh_weight` | the fresh rooms' weight, 1 decimal |
| `inputs/room/<i>/value_c` | for each configured room `i`: its last value, 2 decimals, or `-` |
| `inputs/room/<i>/fresh` | `1` when the room is fresh at the record's instant, else `0` |
| `inputs/room/<i>/weight` | its weight, 1 decimal |

The five input values are `-` when the decision has no inputs: the core resolved nothing then.

**Status**, 10 entries:

| Subtopic | Value |
|---|---|
| `status/demand` | the demand's name: `HEATING`, `COOLING` or `NO_DEMAND` |
| `status/reason` | the reason's name, such as `WAITING_FOR_INPUTS` |
| `status/phase` | the phase's name, such as `RUNNING` |
| `status/energy_wh` | the energy, 1 decimal |
| `status/p_net_w` | the net power, 0 decimals, or `-` without inputs |
| `status/clamped` | `1` or `0` |
| `status/integrated_s` | 0 decimals |
| `status/skipped_s` | 0 decimals |
| `status/uptime_s` | 0 decimals |
| `status/enabled` | `1` or `0` |

**Outputs**, 3 entries:

| Subtopic | Value |
|---|---|
| `outputs/router` | `IDLE`, `WAITING` or `FAULT` |
| `outputs/thermostat_state` | the record's `sent_state`, the state the router last sent, or `-` before the first send |
| `outputs/commands` | the record's commands as `name:value` joined by `,`, such as `SetExternalThermostat1State:2,SetHeatCoolMode:1`, or `-` |

**Settings.** `ctl_telemetry_setting` gives, for index `i` of `ctl_settings_name_at`, the subtopic
`config/<name>` and the value text of `ctl_settings_get`: `config/enabled`, `config/room.0.topic`,
… `config/max_retries`, in the order of the settings' table above. It returns false past the last
name.

**A refusal writes nothing.** Both functions return false, and leave both buffers as they were,
for an index past the end, for a buffer too small for the subtopic or the value with its
terminating zero, and for a number the fixed format refuses: a text is never cut.
