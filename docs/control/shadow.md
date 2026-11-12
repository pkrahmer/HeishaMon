# The shadow

The shadow runs the whole control path live, on the owner's spare D1 mini, fed from the house: the
room thermostats and HeishaMon's values arrive as they do for the legacy controller, the loop
(`loop.md`) decides every tick interval, and every decision is written to a CSV file on the PC. A
night of records shows what the control core would have done, beside what the house did.

Three parts make it:

- **The shadow sketch**, `tests/control/device/shadow/shadow.ino`, on the D1 mini. It runs the
  shadow protocol (`HeishaMon/src/control/loop/ctl_shadow.h`) over the USB serial port at
  230,400 baud, with the board's millisecond clock widened by `ctl_clock`.
- **The host twin**, `tests/control/device/shadow_host.c`. It runs the same protocol over standard
  input and output with the host's monotonic clock, optionally sped up (`--speed <factor>`), so
  that a test covers many decisions in seconds.
- **The bridge**, `tests/control/device/shadow.py`, on the PC. It configures and starts the board
  or the twin, subscribes to the owner's MQTT broker, forwards each message as an input line and
  writes the records and the inputs to CSV files.

## Nothing reaches the pump

1. **The board is not wired to it.** The D1 mini has only its USB cable; no pin of it leads to
   the heat pump or to the HeishaMon board.
2. **The router's commands are only written into the records.** The loop runs with
   `assume_landed` true: it counts every command as landed and never waits for the pump, and the
   sketch has no code that sends a command anywhere but into the `cmd=` field of a record line.
3. **The bridge only subscribes.** `shadow.py` never publishes to the broker; a forwarded message
   goes to the board over the serial line and nowhere else.

## Wiring

Only the USB cable, from the D1 mini to the PC that runs the bridge. It powers the board and
carries the serial line. Nothing else is connected.

## Flashing the D1 mini

On the PC the board is plugged into, with arduino-cli and the ESP8266 core installed as
`device-test.md` shows for the replay, from the root of the checkout:

```sh
arduino-cli board list    # shows the port: COM5 on Windows, /dev/ttyUSB0 on Linux
arduino-cli compile --fqbn esp8266:esp8266:d1_mini \
  --library HeishaMon/src/control/adapt --library HeishaMon/src/control/core \
  --library HeishaMon/src/control/loop --library HeishaMon/src/control/route \
  --library HeishaMon/src/control/wire \
  --output-dir build/shadow tests/control/device/shadow
arduino-cli upload -p <port> --fqbn esp8266:esp8266:d1_mini --input-dir build/shadow
```

Each folder of `HeishaMon/src/control/` is one `--library`. `make device` runs the same compile,
and fails when the sketch does not build. Flashing the shadow replaces the replay sketch on the
board; flash the replay again before `make device-replay`.

## The configuration

A JSON file; `tests/control/device/shadow.example.json` holds placeholders to copy from:

```json
{
  "broker": {"host": "broker.example", "port": 1883},
  "rooms": [
    {"topic": "home/rooms/living/temperature", "weight": 1.0},
    {"topic": "home/rooms/kitchen/temperature", "weight": 1.0},
    {"topic": "home/rooms/bedroom/temperature", "weight": 1.0},
    {"topic": "home/rooms/bath/temperature", "weight": 1.0}
  ],
  "outdoor": "panasonic_heat_pump/main/Outside_Temp",
  "heat": "panasonic_heat_pump/main/Heat_Power_Production",
  "valve": "panasonic_heat_pump/main/ThreeWay_Valve_State",
  "defrost": "panasonic_heat_pump/main/Defrosting_State",
  "pv": null,
  "room_timeout_s": 10800,
  "solar_gain_factor": 0.02,
  "cooling": false
}
```

| Field | Meaning |
|---|---|
| `broker` | the MQTT broker: `host` (a name or address) and `port` (1 to 65535) |
| `rooms` | 1 to 16 rooms, in the order the loop numbers them from 0: each a `topic` and a `weight` (0 to 1000; 0 watches the room without counting it) |
| `outdoor` | the topic of the outdoor temperature |
| `heat` | the topic of the heat power production in W |
| `valve` | the topic of the three-way valve state (0 heating, 1 hot water) |
| `defrost` | the topic of the defrost state, or `null` |
| `pv` | the topic of the PV power in W, or `null` |
| `room_timeout_s` | a room older than this leaves the average |
| `solar_gain_factor` | the share of the PV power the core counts as solar gain |
| `cooling` | `true` allows the core to demand cooling; `false` masks it |

A missing or unknown key, a wrong type or a room count outside 1 to 16 makes the bridge exit with
2 before it opens the link. The board checks the values' ranges itself at the start (`G`) and
answers `bad`; the bridge then stops with 1.

## Running the bridge

The bridge needs paho-mqtt (`python3 -m pip install paho-mqtt`) to talk to the broker and pyserial
to talk to the board. Against the board, from the root of the checkout:

```sh
python3 tests/control/device/shadow.py --config shadow.json --port COM5 --out shadow-out
```

Against the host twin, built as `make test` builds it:

```sh
python3 tests/control/device/shadow.py --config shadow.json --command "./shadow_host" --out shadow-out
```

- `--minutes <m>` ends the run after that many minutes; without it, the run goes on until Ctrl+C.
- `--replay <capture>` reads messages from a capture file instead of the broker: one JSON object
  per line with `after_s` (wall seconds after the start), `topic`, `payload` and `retain`.
  `tests/control/device/data/shadow_capture.jsonl` is the one `make test` uses.

With `--port` the bridge pulses the board's reset and waits for its `ready`, as the replay does.
It then sends the configuration and `G`, and stops with exit 1 when a reply is not `ok`. Then it
subscribes to every configured topic and forwards each message as `I <input> <room> <payload>`.
A message whose payload is not printable ASCII once its line ends are removed, or whose line would
be longer than 240 characters, is not sent and counts as skipped. Retained messages are forwarded
like any other (`decisions.md`).

At the end it prints one line and exits with 0:

```
shadow: 11 records, 11 inputs (10 taken, 1 no reading, 0 refused, 0 bad), 0 skipped, 0 broken lines
```

All three files go to the folder `--out` and are appended to, so that one folder can hold several
nights; a CSV file gets its header row when it is new.

- `decisions.csv`: one row per record.
- `inputs.csv`: one row per message on a configured topic.
- `errors.log`: one line per broken frame, unexpected reply or input left without a reply, with
  the wall time; bytes that are not printable ASCII are written as `\xNN`.

## The columns

`decisions.csv`: `wall_time`, the wall time the line arrived in ISO 8601 UTC
(`2026-11-12T22:15:00.123456Z`), then one column per field of the record, in its order (`loop.md`,
*The record's text*): `t`, `demand`, `reason`, `phase`, `E`, `pnet`, `indoor`, `outdoor`,
`produced`, `solar`, `rooms`, `router`, `cmd`, and `heap`, the board's free heap in bytes (0 from
the twin). `t` is the board's clock in seconds since it booted, `wall_time` the PC's.

`inputs.csv`:

| Column | Content |
|---|---|
| `wall_time` | the wall time the message arrived, ISO 8601 UTC |
| `input` | `room`, `outdoor`, `heat`, `valve`, `defrost` or `pv` |
| `room` | the room's number, 0 for every other input |
| `topic` | the MQTT topic |
| `payload` | the payload without its line ends |
| `retained` | `yes` for a retained message, `no` otherwise |
| `reply` | `taken`, `no_reading`, `refused` or `bad`; `skipped` for a message not sent; `-` when no reply came; any other text is also in `errors.log` |

## Reading a night

- **The reasons over time.** Plot or count `reason` against `wall_time`. A healthy night starts
  with `WAITING_FOR_INPUTS` until every required signal arrived, has one `SEEDED`, then `NORMAL`.
  `HELD_STALE_INPUT` marks a decision that kept the last demand because a signal went older than
  its maximum age; `FAULT_TIMEOUT` a hold that lasted too long, and `FAULT_RECOVERED` the way back
  (`core.md`). Long stretches of `HELD_STALE_INPUT` point at a source that went quiet.
- **The demand and the commands.** `demand` and `cmd` show when the core would have switched the
  heating and with which commands; `E` shows the energy deviation that led there.
- **The rooms.** `rooms=3:3.0/4.0` says that 3 rooms, weighing 3.0 of 4.0, were fresh at that
  decision. A count that drops points at a thermostat that stopped reporting.
- **The rooms' report cadence.** In `inputs.csv`, take the rows of `input` `room`, group them by
  `room` and look at the gaps between their `wall_time`s: the longest gap of a room must stay well
  below `room_timeout_s`, or that room falls out of the average.
- **The link.** `errors.log` should stay empty; the summary's `broken lines` count the frames the
  serial line broke.

## The protocol

Every line in both directions carries the frame of `ctl_frame` (`device-test.md`, *The frame*); a
line whose frame does not check is answered with `error`. The board starts with `ready`, answers
each line with one reply, and writes each record as `D <record> heap=<free heap>` as soon as it is
due. Request lines are at most 240 characters without frame and line end.

| Request | Effect | Reply |
|---|---|---|
| `K rooms <n>` | the room count, 1 to 16; every weight back to 1.0 | `ok` |
| `K weight <i> <w>` | the weight of room `i` | `ok` |
| `K room_timeout_s <s>` | the room timeout | `ok` |
| `K solar_gain_factor <g>` | the core's `solar_gain_factor` | `ok` |
| `K cooling <0 or 1>` | 1 adds `COOLING` to `allowed`, 0 removes it | `ok` |
| `G` | `ctl_loop_config_valid`; when valid, `ctl_loop_init` at now and started | `ok`, or `bad` and not started |
| `I <input> <room> <payload>` | `ctl_loop_input`; `<input>` is `room`, `outdoor`, `heat`, `valve`, `defrost` or `pv`; `<room>` decimal; `<payload>` everything after the third space | `taken`, `no_reading`, `refused` or `bad` |
| `S` | | `stats inputs=<n> records=<n> heap=<n> uptime_s=<n>` |

- Every number in a `K` line is a bare number as the payload parser reads it; a count or an index
  is a whole number, and an index is below the room count.
- A `K` line or a second `G` after the start gives `error`, and so does an `I` line before it.
- `inputs` counts every `I` line that got one of the four input replies; `records` every `D` line.
- Any other line gives `error`: an unknown request or name, a missing or surplus field, a field
  that does not parse, a value outside its range, and a line longer than 240 characters.
- The shadow starts with the loop's defaults for 4 rooms and `assume_landed` true.
