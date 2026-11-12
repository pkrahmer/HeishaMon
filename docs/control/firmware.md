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

## The glue

`glue/ctl_firmware.cpp` is the only code of the loop that knows HeishaMon. It is Arduino C++,
compiled only by `make firmware`; everything it decides is in the pure modules beside it, which
the host tests cover. `glue/ctl_dispatch.h` is its pure half: which input or setting a topic is,
and the thermostat state of the emulated frame. The glue never waits and never retries a publish
within one call, its state is static, and it allocates nothing of its own after setup. Upstream's
decoder (`getDataValue`) returns an Arduino `String`; that allocation is upstream's.

## The hooks

Six lines in upstream's `HeishaMon/HeishaMon.ino` call the glue, each ending with the comment
`// control loop hook`, so that `grep -c "control loop hook" HeishaMon/HeishaMon.ino` gives 6 and a
rebase onto upstream has six lines to carry:

1. `#include "src/control/glue/ctl_firmware.h"`, beside the other includes of `src/`.
2. `ctl_firmware_setup(send_command, log_message);` in `setup()`, right after
   `setupConditionals()`. By then the file system is mounted and upstream's settings are loaded,
   and the HTTP server and the MQTT client that `log_message` writes to exist, so the glue's
   start-up log lines reach every place upstream's do.
3. `ctl_firmware_loop(...)` in `loop()`, right after `mqtt_client.loop();`. Every pass of
   upstream's loop reaches it, connected to the broker or not, so the clock is widened often and
   the loop ticks on time; it comes after the client's own pass, so that a decision publishes on a
   connection the client has just served. It returns at once when no tick is due.
4. `ctl_firmware_frame(data);` right after the `decode_heatpump_data(data, ...)` of a main data
   block, in `readSerial()`, which `readHeatpump()` calls. That is the one place a decoded main
   frame passes, and it is not behind a test of the broker's connection, so the pump's values
   reach the loop while the broker is down (decision F-3).
5. `ctl_firmware_subscribe(...)` in `mqtt_reconnect()`, after upstream's subscriptions of a
   successful connect: every new connection subscribes again to the loop's topics and publishes
   the settings again.
6. `if (ctl_firmware_mqtt(topic, payload, length)) return;` at the very start of
   `mqtt_callback()`, before upstream's own guard and copies: a message of the loop's is handled
   and never reaches upstream's parsing; every other message goes on untouched.

## What reaches the pump

Commands go to the pump only when four things hold:

- `enabled` is 1;
- HeishaMon's optional PCB emulation is on, since the thermostat state is a field of the frame
  it emulates;
- HeishaMon is not in listen-only mode, in which it sends nothing to the pump;
- the router has a command at this decision.

Otherwise the loop still decides and publishes, and `status/enabled` is `0`: it is `1` only when
the first three hold, so that it says whether a command would reach the pump. The glue writes one
log line on each change of the first three, at the first pass and whenever one of them changes:
`control: commands go to the pump`, or `control: no commands to the pump:` with the first of them
that fails (`enabled is 0`, `the optional PCB emulation is off`, `HeishaMon is in listen-only
mode`).

A command goes through upstream's `send_heatpump_command`, the path of the `commands/` topics and
the rules engine. `SetExternalThermostat1State` writes the state into byte 6 of the emulated frame,
`optionalPCBQuery`, which HeishaMon sends to the pump every second.

**The confirmation is the emulated frame itself.** On every pass the glue reads bits 2 and 3 of
byte 6 of `optionalPCBQuery` (`ctl_dispatch_thermostat_state`) and hands them to the router with
`ctl_loop_confirm`: a command has landed when the frame carries its state. The state counts as
known only while the frame goes out, with the emulation on and outside listen-only mode;
otherwise the router repeats and then faults, as for a dropped command.

**Opening the gate reopens the router, paced.** While commands cannot go out, the router steps as
if it sent them, and may give up on a state the pump never got. When the gate opens again after
the first pass, the glue reopens the router (`ctl_router_reopen`): it forgets what it sent and that
it gave up, but keeps the instant of its last send. The wanted state then goes to the pump once,
and not sooner than `min_change_interval_s` after that last send, so reopening never sends more
often than the router's pacing allows.

## The settings file

The settings live in `/control.json` on the board's file system: one JSON object of names to
text values, the names and value texts of the settings' table above.

    {"enabled":"0","cooling":"0","rooms":"2","room.0.topic":"ccu/living/temp",
     "room.0.weight":"1","room.1.topic":"ccu/bath/temp","room.1.weight":"0.5",...}

At start the glue applies the pairs in file order with `ctl_settings_set`, so `rooms` comes before
the rooms' names. A value that is not a JSON string, or a pair the setter refuses, is logged with
the setter's answer and skipped; the rest of the file still applies. A missing file, one that
cannot be opened or does not hold a JSON object, and a set that ends invalid give the defaults
for every setting, and the reason is logged.

The glue writes the file whenever a setting changes over MQTT, every setting in the order of
`ctl_settings_name_at`. It writes `/control.json.new` first and renames it over `/control.json`,
so that a reset while writing leaves the old file. A failed write is logged; the change then
holds until the next start.

## Changing a setting over MQTT

A setting changes by a message to `<base>/control/set/<name>`, its payload the value text, such as
`panasonic_heat_pump/control/set/hysteresis_wh` with `1500`. The glue hands it to
`ctl_settings_set`. On `OK` it saves the file and applies the new settings from the next tick
without restarting the loop: the core keeps its energy and phase, a change of `cooling` sets the
allowed demands at once, and a change of `rooms`, a room's topic or `pv_topic` unsubscribes the
old input topics and subscribes the new ones.

In every case the glue answers with the setting's current value, retained, on
`<base>/control/config/<name>`: the new value on `OK`, the old one on a refusal, which is also
logged with the setter's answer. A name the settings do not list (a room at or above `rooms`
included) has no value to answer with: it is logged, and nothing is published. A payload that
does not fit a value text, or holds a zero byte, is refused like a bad value.

Only `config/<name>` of the message's own name is answered. Every other setting is published at
the next connect, when the glue publishes every listed setting retained. So after `rooms` is
raised, the new rooms' `config/room.<i>.*` appear only at the next connect, and after it is
lowered, the retained `config/room.<i>.*` of the dropped rooms stay on the broker until they are
cleared by hand.

## First start on a board

A board fresh from the flash, or one whose `/control.json` was lost, starts with the defaults:
commands disabled, no rooms, no PV. It runs the loop and publishes, but sends nothing.

1. Set the rooms first: `control/set/rooms`, then each `control/set/room.<i>.topic` and, where it
   is not 1, `control/set/room.<i>.weight`; `pv_topic` if there is PV. Check every answer under
   `control/config/`.
2. Make sure the optional PCB emulation is on and listen-only mode is off in HeishaMon's settings.
3. Watch `control/inputs/` until every room and the pump's values arrive, and `control/status/`
   until the phase runs and the demands it shows make sense for the house. The log says why no
   command goes to the pump.
4. Only then set `control/set/enabled` to `1`, and watch `control/outputs/` and the pump.
