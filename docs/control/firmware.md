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
| `outdoor_topic` | text | empty | the outdoor temperature from MQTT; see *Shadowing another HeishaMon* |
| `heat_topic` | text | empty | the produced heat from MQTT; see *Shadowing another HeishaMon* |
| `valve_topic` | text | empty | the three-way valve from MQTT; see *Shadowing another HeishaMon* |
| `defrost_topic` | text | empty | the defrost flag from MQTT; see *Shadowing another HeishaMon* |
| `frame_log_topic` | text | empty | another HeishaMon's log topic; see *Frames from another HeishaMon's log* |
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
| `smart_grid` | `0` or `1` | `0` | `1` sends Smart Grid mode instead of the thermostat state; see *Commands to another HeishaMon* |
| `command_topic` | text | empty | another HeishaMon's commands topic; see *Commands to another HeishaMon* |

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
timeout; the router's pacing, polarity and `smart_grid`; the allowed demands, heating and no demand, with
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

**Inputs**, 9 entries, 3 for each configured room and the 2 frame counters:

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
| `inputs/frames_decoded` | the main data frames from `frame_log_topic` decoded since the start |
| `inputs/frames_dropped` | the frames from `frame_log_topic` dropped since the start |

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

**Outputs**, 4 entries:

| Subtopic | Value |
|---|---|
| `outputs/router` | `IDLE`, `WAITING` or `FAULT` |
| `outputs/thermostat_state` | the record's `sent_state`, the state the router last sent (the Smart Grid mode while `smart_grid` is 1), or `-` before the first send |
| `outputs/commands` | the record's commands as `name:value` joined by `,`, such as `SetExternalThermostat1State:2,SetHeatCoolMode:1`, or `-` |
| `outputs/blocked` | why commands do not reach the pump: `disabled`, `listen_only`, `no_optional_pcb` or `no_frame_log`, or `-` while the gate is open; see *What reaches the pump* |

**Settings.** `ctl_telemetry_setting` gives, for index `i` of `ctl_settings_name_at`, the subtopic
`config/<name>` and the value text of `ctl_settings_get`: `config/enabled`, `config/room.0.topic`,
… `config/command_topic`, in the order of the settings' table above. It returns false past the last
name.

**A refusal writes nothing.** Both functions return false, and leave both buffers as they were,
for an index past the end, for a buffer too small for the subtopic or the value with its
terminating zero, and for a number the fixed format refuses: a text is never cut.

## The glue

`glue/ctl_firmware.cpp` is the only code of the loop that knows HeishaMon. It is Arduino C++,
compiled only by `make firmware`; everything it decides is in the pure modules beside it, which
the host tests cover. `glue/ctl_dispatch.h` is its pure half: which input or setting a topic is,
and the thermostat state of the emulated frame. The glue never waits and never retries a publish
within one call, its state is static, and it allocates nothing of its own after setup but the
argument buffer of a save from the control page (below), freed once the save is applied. Upstream's
decoder (`getDataValue`) returns an Arduino `String`; that allocation is upstream's.

## The hooks

Eight lines in upstream's `HeishaMon/HeishaMon.ino` call the glue, each ending with the comment
`// control loop hook`, so that `grep -c "control loop hook" HeishaMon/HeishaMon.ino` gives 8 and a
rebase onto upstream has eight lines to carry. The first six are these; the last two, and one
line of `HeishaMon/htmlcode.h`, serve the control page and are described with it below.

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

Commands go to the pump only when the gate is open and the router has a command at this
decision. The gate is `ctl_gate_state` (`HeishaMon/src/control/glue/ctl_gate.h`), computed on
every pass. With `command_topic` set, the commands go to another HeishaMon and the gate is that
of *Commands to another HeishaMon*. Otherwise it is blocked for the first of these reasons that
holds, in this order:

- `disabled`: `enabled` is 0;
- `listen_only`: HeishaMon is in listen-only mode, in which it sends nothing to the pump;
- `no_optional_pcb`: HeishaMon's optional PCB emulation is off, and the thermostat state is a
  field of the frame it emulates.

Otherwise the gate is open. While it is blocked the loop still decides and publishes,
`status/enabled` is `0` (it is `1` only while the gate is open, so that it says whether a command
would reach the pump), and `outputs/blocked` names the reason (`ctl_gate_name`); it is `-` while
the gate is open. The glue writes one log line on each change of the gate, at the first pass and
whenever it changes: `control: commands go to the pump`, or `control: no commands to the pump:`
with the reason (`enabled is 0`, `HeishaMon is in listen-only mode`, `the optional PCB emulation
is off`, `command_topic is set, frame_log_topic is empty`).

A command goes through upstream's `send_heatpump_command`, the path of the `commands/` topics and
the rules engine, or with `command_topic` set over MQTT to another HeishaMon. Two paths reach the
pump through byte 6 of the optional PCB frame, which HeishaMon sends to the pump every second:

- **The thermostat path** (`smart_grid` 0): `SetExternalThermostat1State` writes the state (2 heat,
  1 cool, 0 no demand) into bits 2 and 3, and `SetHeatCoolMode` rides along when
  `heat_cool_heat_value` is set.
- **The Smart Grid path** (`smart_grid` 1): `SetSmartGridMode` alone writes the mode into bits 4
  and 5: `0` for heating or cooling demand, `1` (Capacity 1, set on the owner's pump to a curve
  that never heats) for no demand. Heating and cooling are the same
  mode, so a change between them sends nothing. Pacing, repeats, the fault and the reopening are
  those of the thermostat path.

**The confirmation is the optional PCB frame itself.** On every pass the glue reads byte 6 of
the frame (bits 2 and 3, `ctl_dispatch_thermostat_state`, or with `smart_grid` 1 bits 4 and 5,
`ctl_dispatch_smart_grid_mode`) and hands the value to the router with `ctl_loop_confirm`: a
command has landed when the frame carries the value sent. Without `command_topic` the frame is
this board's emulated one, `optionalPCBQuery`, and the value counts as known only while it goes
out, with the emulation on and outside listen-only mode; with it, the frame is the other board's,
as its log shows it.

**While the gate is blocked, landing is assumed.** Before each tick the glue sets the loop's
`assume_landed` to whether the gate is blocked: the router then counts each command as landed at
the next decision, as a shadow does. A blocked board, such as a spare D1 that shadows the house
with `enabled` 0, sends nothing, so nothing could ever confirm; without the assumption its router
would repeat its first send and end in `FAULT`, a fault of a link it never used. While the gate is
open the confirmation is the frame's, as above, and a command that does not land is repeated and
then reported as `FAULT`.

**Opening the gate reopens the router, paced.** While commands cannot go out, the router steps as
if it sent them and counts them as landed, so it believes the pump holds a state it never got.
When the gate opens again after the first pass, the glue reopens the router (`ctl_router_reopen`):
it forgets what it sent and whether it gave up, but keeps the instant of its last send. The wanted state then goes to the pump once,
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

The glue writes the file on every accepted setting over MQTT, and on a save from the control
page that changed a value, every setting in the order of
`ctl_settings_name_at`. It writes `/control.json.new` first and renames it over `/control.json`,
so that a reset while writing leaves the old file. A failed write is logged; the change then
holds until the next start.

## Changing a setting over MQTT

A setting changes by a message to `<base>/control/set/<name>`, its payload the value text, such as
`panasonic_heat_pump/control/set/hysteresis_wh` with `1500`. The glue hands it to
`ctl_settings_set`. On `OK` it saves the file and applies the new settings from the next tick
without restarting the loop: the core keeps its energy and phase, a change of `cooling` sets the
allowed demands at once, and a change of `rooms`, a room's topic, `pv_topic`, a pump topic or
`frame_log_topic` unsubscribes the old input topics and subscribes the new ones.

In every case the glue answers with the setting's current value, retained, on
`<base>/control/config/<name>`: the new value on `OK`, the old one on a refusal, which is also
logged with the setter's answer. A name the settings do not list (a room at or above `rooms`
included) has no value to answer with: it is logged, and nothing is published. A payload that
does not fit a value text, or holds a zero byte, is refused like a bad value.

The client has one buffer for what it receives and what it sends, so the glue reads or copies a
message's topic and payload before it calls anything that can publish, subscribe, unsubscribe or
log.

Only `config/<name>` of the message's own name is answered. Every other setting is published at
the next connect, when the glue publishes every listed setting retained. So after `rooms` is
raised, the new rooms' `config/room.<i>.*` appear only at the next connect, and after it is
lowered, the retained `config/room.<i>.*` of the dropped rooms stay on the broker until they are
cleared by hand.

## The control page

HeishaMon's web interface has a page of the loop, `/control`, linked as *Control* in the menu of
the root page. From top to bottom it shows:

- what the last save from the page accepted and refused, by name, if the board saved since its
  start (a save that held no setting says so);
- the table *Status*, with the status and output entries of the telemetry of the last decision,
  and the table *Inputs*, with its input entries, each by its subtopic and value text as MQTT
  carries them; before the first decision, a line saying that no decision has been made yet;
- a form with every setting, by name, prefilled with its current value as `ctl_settings_get`
  writes it, and a button *Save*.

A value whose escaped row does not fit the page's 512-byte chunk, such as a topic full of quotes,
shows its name and no field, so that a save leaves it as it is; it can still be set over MQTT.

**Saving.** The form posts to `/savecontrol`. The page keeps every field until the whole form has
arrived, then walks the names of `ctl_settings_name_at` in their order, reading the count of
names again after each one, and applies the field of each name it finds with `ctl_settings_set`.
So `rooms` applies before the rooms' fields whatever order the browser sends, and a field the
list does not hold (a room at or above the count) is ignored. Upstream's web server reads the
body through a small buffer (128 bytes on the ESP8266) and hands a longer field over in pieces,
one step each under the same name; the page joins the pieces again, so a long topic, whose every
`/` the browser sends as `%2F`, arrives whole. A field whose joined value is too long for any
setting, or holds a zero byte, is refused whole and never cut. When any value changed, the glue
writes `/control.json` as above, and applies the settings as a setting topic does: from the next
tick, without restarting the loop; a change of `cooling` sets the allowed demands at once with
`ctl_set_allowed`. Since the form holds every topic, a save unsubscribes the input topics before
it applies and subscribes the current ones after, and when anything changed it publishes every
setting again under `control/config/`. The answer redirects to `/control`, which then lists every
name the setter accepted as *accepted*, the value it already had included, and every name the
setter refused as *refused*; a refusal is also logged with the setter's answer, and the refused
setting keeps its old value. A save that only repeats the current values writes no file.

The keeping, joining, ordering and applying of the fields, and the HTML of the report, are the
pure module `glue/ctl_form.c`, which `tests/control/test_form.c` covers on the host; it applies
through a setter it is given, here the glue's `ctl_firmware_change`. `glue/ctl_web.cpp` only
hands it the web server's steps.

**Adding rooms.** The form shows the fields of the rooms below the current count only. To add
rooms, set `rooms` to the new count and save first; the page then shows the new rooms' topic and
weight fields, which a second save sets. Lowering the count and saving drops the fields above it.

**Memory.** Every piece of the page the glue builds fits one buffer of 512 bytes on the stack, and
the page goes out one content step at a time, as upstream's Modbus page does. A save allocates
one buffer for the names and values of every setting at their maximum lengths when the request
for `/savecontrol` arrives and frees it right after applying it, before the answer goes out; a
connection lost before that frees it at its close. What the last save accepted and refused stays
in a static list of every setting's name, `CTL_SETTINGS_NAMES_MAX` long.

**The hook lines.** Two lines of `HeishaMon/HeishaMon.ino` and one of `HeishaMon/htmlcode.h`
carry the page:

7. `#include "src/control/glue/ctl_web.h"`, beside the include of the glue.
8. `{ int8_t result; if (ctl_web(client, dat, &result)) return result; }` as the first statement
   of `webserver_cb`. Upstream's web server calls that one callback for every step of every
   request (the method, the URI, each argument, the answer's header, each content step and the
   close), and `client->route` is the only state that ties the steps of one request together.
   `ctl_web` claims `/control` and `/savecontrol` at the URI step by setting routes of its own,
   240 and 241, which no upstream route uses, and from then on recognizes every later step of
   the request by that route. So one line at the very start serves both routes and every step,
   and every other request passes on to upstream untouched. `/savecontrol` is claimed even after
   the method step has set upstream's route for a POST.
9. In `htmlcode.h`, the menu of the root page gets the link
   `<a href="/control"><span class="nav-icon">&#9832;</span> Control</a>` after the link to
   `/rules`.

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

## Shadowing another HeishaMon

A board without a pump never decodes a frame, so its loop never gets the pump's values. Four
settings name MQTT topics for them instead, as another HeishaMon publishes them under
`<its base>/main/`:

| Setting | Input | Topic of a HeishaMon with base `jeisha` |
|---|---|---|
| `outdoor_topic` | the outdoor temperature | `jeisha/main/Outside_Temp` |
| `heat_topic` | the produced heat | `jeisha/main/Heat_Power_Production` |
| `valve_topic` | the three-way valve | `jeisha/main/ThreeWay_Valve_State` |
| `defrost_topic` | the defrost flag | `jeisha/main/Defrosting_State` |

The glue subscribes to every one that is set and hands its messages to the loop as that input,
through `ctl_loop_input`, as it does a room's or PV's message; `ctl_dispatch_topic` only picks the
input by the topic. A topic equal to a room's topic is the room's; one equal to `pv_topic` is PV;
one that two of the four share goes to the first of outdoor, heat, valve and defrost.

**While any of the four is set, the frames hand nothing.** `ctl_settings_pump_from_mqtt` is then
true, and `ctl_firmware_frame` hands no value to the loop, so that a board never mixes the values
of two pumps; a value whose topic is left empty then never arrives, and the core treats it as
missing. The glue logs where the pump's values come from at setup and whenever that source
changes: `control: the pump values come from MQTT, the frames hand none` or `control: the pump
values come from this board's frames`. A board on a pump leaves the four empty and behaves as before.

To run the firmware on a spare board beside a production HeishaMon, whose base is `jeisha` here:

1. Flash the spare board and give it its own MQTT base in HeishaMon's settings, such as
   `jeisha_shadow`, so that its values, its `control/` topics and its setting topics never mix
   with the production board's.
2. Turn listen-only mode on in its HeishaMon settings: it has no pump to talk to, and it must send
   nothing.
3. Leave `control/set/enabled` at `0`, the default, so that no command is ever queued for a pump.
4. Set the four pump topics, each by `<its base>/control/set/<name>`: `outdoor_topic` to
   `jeisha/main/Outside_Temp`, `heat_topic` to `jeisha/main/Heat_Power_Production`,
   `valve_topic` to `jeisha/main/ThreeWay_Valve_State` and `defrost_topic` to
   `jeisha/main/Defrosting_State`. Check the answers under `control/config/` and the log line that
   the pump values come from MQTT.
5. Set `rooms`, each `room.<i>.topic` to the same room topics the house's sensors publish, and
   `pv_topic` if there is PV, as on a first start (*First start on a board*).
6. Watch `control/inputs/` until every room and the pump's values arrive, and then compare the
   spare board's `control/status/` and `control/outputs/` with what the production board does.

## Frames from another HeishaMon's log

The four pump topics give the loop four values; a board that decodes the pump's frames itself
publishes all of them under its own `main/`. With `frame_log_topic` set, a board without a pump
rebuilds those frames from another HeishaMon's log and decodes them as if it were wired to the
pump.

**What the production board must have on.** In its HeishaMon settings: hex dumps (*Debug hexdump*,
or its switch in the console) and log to MQTT (*Debug log to MQTT*). Its log then reaches
`<its base>/log`, one message per line, and every frame it receives appears as a
`Received <n> bytes data` line, the frame's `data:` lines of up to 32 hex bytes and a checksum line.
Set `frame_log_topic` on the spare board to that topic, such as `jeisha/log`.

**The four pump topics must be empty.** While any of them is set, `ctl_firmware_frame` hands
nothing to the loop (*Shadowing another HeishaMon*), so the frames from the log would be
published but never reach the loop. Use one source of the pump's values or the other.

**What the spare board does.** It subscribes to `frame_log_topic` and feeds each message, in the
order it arrives, to the pure parser `ctl_framelog_line`
(`HeishaMon/src/control/glue/ctl_framelog.c`). The parser reads the text after the first `): `,
starts a frame at `Received <n> bytes data`, appends the bytes of each `data:` line and gives the
frame when the announced count is reached and the sum of its bytes is 0 modulo 256, as upstream's
own receive check. A frame the other board sends, logged as a
`sent bytes: <n> including checksum value: <c>` line and the `data:` lines of all its bytes but the
checksum, is rebuilt the same way, the checksum appended from the header, and given as `SENT`; it
is never decoded, and only its optional PCB frame is kept (*Commands to another HeishaMon*). Any
`sent bytes:` line ends a frame in progress, and one of another form starts none. A main data frame, 203 bytes with `0x71` first and `0x10` in byte 3
(`ctl_framelog_is_main_data`), goes to upstream's `decode_heatpump_data` with the spare board's own
globals, so the spare board publishes every decoded value under its own `<base>/main/`, as a board
on the pump does, and then to `ctl_firmware_frame`, so its loop takes the outdoor temperature, the
produced heat, the valve and the defrost flag from it.

**The counters.** `control/inputs/frames_decoded` counts the main data frames handed to the
decoder; `control/inputs/frames_dropped` counts the frames cut short (a missing line, a new
`Received` line or a `sent bytes:` line before the end, more bytes than announced, a malformed
byte) and the ones whose checksum fails, sent ones included: only frames that broke. A whole
frame that is no main data frame (an optional PCB answer, the extra data block) and a whole frame
the other board sent count as neither. Both count from the start and go out with
the rest of the telemetry. A dropped count that grows as fast as the decoded one means the log
loses lines: the broker drops messages, or the production board logs too much between them.

**The risk on the production side.** HeishaMon drops its MQTT connection when a log publish
fails, and with hex dumps on it publishes about ten log messages for every frame. A slow broker or
a weak WiFi link on the production board can then make it reconnect often, which also delays its
own values and commands. Turn hex dumps and log to MQTT off again on the production board when
the spare board no longer needs them.

## Commands to another HeishaMon

A board without a pump of its own, such as the owner's dev board, can run the house's pump
through the production board: it sends its commands as MQTT messages to the production board's
command topics and confirms them from the production board's log.

**What the dev board needs.** In its control settings:

- `frame_log_topic`: the production board's log topic, such as `jeisha/log`, set up as in
  *Frames from another HeishaMon's log*: it gives the pump's values and the confirmation;
- `command_topic`: the production board's commands topic, `<its base>/commands`, such as
  `jeisha/commands`;
- `smart_grid`: `1` for a pump that obeys Smart Grid mode;
- `enabled`: `1`, last, once the rest is set.

The production board must have its optional PCB emulation on: its optional PCB frame carries the
Smart Grid mode to the pump and into its log. Listen-only mode and the optional PCB setting of
the dev board play no part; they concern its own serial port.

**How a command goes out.** For each command of the router, the glue publishes one message, not
retained, topic `<command_topic>/<name>` and payload the value, such as
`jeisha/commands/SetSmartGridMode` with `1`, instead of calling `send_heatpump_command`.
Upstream HeishaMon subscribes to `<base>/commands/#` and executes such a message as if it came
from its own web page. A publish that succeeds writes the log line
`control: published <topic> <value>`. A publish that the MQTT client refuses writes
`control: publish failed <topic> <value>` instead, and a topic that does not fit the topic buffer
is not published and writes `control: command topic too long for <name>, not published`.

**How a command is confirmed.** The production board logs every optional PCB frame it sends; the
dev board rebuilds it from the log (`CTL_FRAMELOG_SENT`, `ctl_framelog_is_optional_pcb`: 20
bytes, `0xF1` first, `0x50` in byte 3) and keeps its byte 6 and the time it saw it. The
reported value is known while such a frame was seen in the last 60 s, and is the Smart Grid mode
of byte 6 (bits 4 and 5) with `smart_grid` 1, or the thermostat state (bits 2 and 3) with 0. A
command has landed when the reported value equals the value sent; otherwise it is repeated and
then reported as `FAULT`, as on the serial path. A change of any topic setting forgets the last
frame, and a change of `smart_grid` or `command_topic` reopens the router (*What reaches the
pump*), so it sends the wanted state once, paced.

**The gate.** With `command_topic` set, it is blocked for the first of these reasons that holds:

- `disabled`: `enabled` is 0;
- `no_frame_log`: `frame_log_topic` is empty, so nothing could confirm a command; the router
  would only repeat its first send and end in `FAULT`.

Otherwise the gate is open. While blocked, landing is assumed, as on the serial path.

**The pump must obey Smart Grid mode.** The owner's pump ignores the external thermostat input of
the optional PCB, so the thermostat path changes nothing there. With `smart_grid` 1 the router
sends only `SetSmartGridMode`: `0` (normal operation) for heating or cooling demand, `1` for no
demand; never `SetExternalThermostat1State` or `SetHeatCoolMode`. Whether the pump heats or
cools stays as the pump is set.

Value `1` is upstream's "Capacity 1" (`OptionalPCB.md`). It stops room heating on the owner's
pump only because the owner set Capacity 1 on the pump itself to a heating curve that never
heats, so hot water goes on; the owner's Node-RED history shows the pump stopping on it every
time. Another pump needs the same installer setting before `smart_grid` is used, or value `1`
only lowers its capacity. Value `2`, "heat pump and hot water off", is never sent, because it
stops hot water too.

**How to switch back.** `enabled` 0 on the dev board stops its commands at once. The production
board keeps the last Smart Grid mode it received: if that was `1`, the pump stays in Capacity 1
until the owner sends `SetSmartGridMode` `0` to the production board, by its web page or its `commands/`
topic, or Node-RED takes over again. Empty `command_topic` sends the dev board's commands to its
own serial port again.
