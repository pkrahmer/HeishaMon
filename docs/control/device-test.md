# The device test

The golden traces (`golden-traces.md`) prove the C core on the host: built with gcc, it reproduces
every value the Python reference recorded, bit for bit. The board runs a different compiler, for a
different processor without a floating-point unit for doubles, with its own runtime library. The
device test replays the same traces through the core **on a D1 mini (ESP8266)** and compares every
reply exactly, so that it proves the board computes every value as the host does.

The parts:

| Part | Path | What it does |
|---|---|---|
| the line protocol | `HeishaMon/src/control/wire/ctl_wire.h`, `ctl_wire.c` | turns one request line into calls on the core and writes one reply line; plain C, pure like the core |
| the test sketch | `tests/control/device/replay/replay.ino` | moves lines between the USB serial port and the protocol, and measures the time each call takes |
| the host twin | `tests/control/device/host_server.c` | does the same over standard input and output on the host |
| the replay script | `tests/control/device/replay.py` | reads the traces, sends their configuration and calls to the sketch or the twin, and compares every reply |

`make test` builds the host twin and replays `edge_cases` and `flaky_transport` through it; it
prints `device replay: 2 files, 10639 calls, 0 mismatches`. `make device` compiles the sketch for
the D1 mini and prints the memory it uses. Neither touches a board: flashing it and replaying all
thirteen traces is a step by hand, below.

## Why doubles cross the line as bit patterns

Every double travels as the 16 lowercase hexadecimal digits of its IEEE-754 bit pattern, most
significant first: `1.0` is `3ff0000000000000`, `-0.0` is `8000000000000000`, a NaN is
`7ff8000000000000`. Decimal text would have to be printed by the board's `printf` and read back by
Python, two conversions that could round or spell a value differently and hide or invent a
difference. The bit pattern carries the value exactly, NaN and the infinities and the sign of zero
included, and needs no floating-point code on the board to write it.

## The protocol

One request per line; a trailing `\r` is ignored; fields are separated by exactly one space. A
request line is at most 128 characters (`CTL_WIRE_LINE_MAX`) without its line end. Every request
gets exactly one reply line.

- Doubles: 16 lowercase hexadecimal digits, as above. Fewer or more digits, uppercase letters and
  any other character are refused.
- Enums: their C values as decimal digits. `ctl_demand`: `HEATING` 0, `COOLING` 1, `NONE` 2;
  `ctl_reason`, `ctl_phase` and `ctl_signal` in their declared order (`core.md`).
- Masks: hexadecimal. A demand mask is one digit (`CTL_DEMAND_BIT` of each permitted demand), a
  signal mask two digits (`CTL_SIGNAL_BIT`), the configuration's problem bits eight digits.

| Request | Effect | Reply |
|---|---|---|
| `R` | `ctl_config_defaults`, `ctl_state_init`, not begun, `calls` 0 | `ok` |
| `C <name> <value>` | sets one field of the configuration; `<name>` is a field of `ctl_config`, or `max_age_s.<i>` / `fallback_w.<i>` with `<i>` the signal's value 0 to 4; `trim_limit_enabled` takes `0` or `1`, every other field a double | `ok` |
| `B` | validates the configuration; when valid, `ctl_state_init`, begun, `calls` 0 | `ok`, or `bad <problem bits>` and not begun |
| `O <t> <signal> <value>` | `ctl_observe` | the state line |
| `A <t>` | `ctl_advance` | the state line |
| `M <t> <demand mask>` | `ctl_set_allowed` | the state line |
| `E <t>` | `ctl_evaluate` | the decision line |
| `S` | answered by the sketch or the twin, not by the protocol | the stats line |

- The state line is `<energy_wh> <integrated_s> <skipped_s> <clamped>`, with `clamped` as `0` or
  `1`.
- The decision line is `<demand> <reason> <phase> <energy_wh> <dt_s> <integrated_s> <skipped_s>
  <clamped> <p_net_w> <unhealthy> <substituted> <produced_wh> <solar_wh> <internal_wh>
  <transmission_wh> <trim_wh>`, with `p_net_w` as `-` when the decision has no inputs.
- Any other line gets `error` and changes nothing: an unknown request, a missing or a surplus
  field, a field that does not parse, a signal above 4, a demand mask above 7, an unknown
  configuration name, `O`, `A`, `M` or `E` before a successful `B`, and a line longer than 128
  characters.
- `C` after `B` is accepted and ends nothing; the next `B` validates and starts again.
- The stats line is `stats calls=<n> us_total=<n> us_max=<n> heap=<n> state_bytes=<n>`: the
  `O`, `A`, `M` and `E` requests answered since the last `B`; the microseconds the board spent in
  `ctl_wire_handle` for them, summed and the largest; the free heap in bytes; and the size of the
  session (configuration and state) in bytes. The host twin writes 0 for the time and the heap.

The sketch and the twin print `ready` once when they can take requests. The sketch runs the
serial port at 921,600 baud with a receive buffer of 1,024 bytes; the script keeps up to 16
requests in flight, which fit in it.

## Flashing the D1 mini

On the PC the board is plugged into, with [arduino-cli](https://arduino.github.io/arduino-cli/)
installed, from the root of the checkout:

```sh
arduino-cli config init   # once, when there is no configuration yet
arduino-cli core update-index --additional-urls https://arduino.esp8266.com/stable/package_esp8266com_index.json
arduino-cli core install esp8266:esp8266@3.1.2 --additional-urls https://arduino.esp8266.com/stable/package_esp8266com_index.json
arduino-cli board list    # shows the port: COM5 on Windows, /dev/ttyUSB0 on Linux
arduino-cli compile --fqbn esp8266:esp8266:d1_mini \
  --library HeishaMon/src/control/core --library HeishaMon/src/control/wire \
  --output-dir build/replay tests/control/device/replay
arduino-cli upload -p <port> --fqbn esp8266:esp8266:d1_mini --input-dir build/replay
```

Each folder of `HeishaMon/src/control/` is one `--library`; a folder added later is one more.
`make device` runs the same compile with every folder, and fails when the sketch does not build.

## Running the replay

The script needs pyserial on that PC (`python3 -m pip install pyserial`). With the sketch
flashed and no serial monitor holding the port:

```sh
make device-replay PORT=<port>
```

It unpacks the thirteen traces, opens the port at 921,600 baud, and waits up to 10 seconds for
`ready` (the board resets when the port opens, and prints its boot messages first; the script
ignores them). It then replays each trace and prints one line per trace, the stats line and a
summary:

```
.../barbarastr_march.trace: 129493 calls, 0 mismatches
...
.../winter_day.trace: 25912 calls, 0 mismatches
stats calls=25912 us_total=... us_max=... heap=... state_bytes=...
device replay: 13 files, 332239 calls, 0 mismatches
```

The stats line covers the last trace only: `us_total / calls` is the mean time of one call on the
board, `us_max` the slowest, `heap` what the sketch leaves free, and `state_bytes` what one
controller's configuration and state take. The script exits with 0 when every trace passed, 1
when one did not, and 2 on a wrong command line, when the port cannot be opened or when no
`ready` came.

## On a mismatch

A value that differs prints `<file>:<line>: <key>: expected <value>, got <value>`, with the
trace's line and key; a trace stops after ten. A reply `error`, or a reply with the wrong number
of fields, to a call prints `<file>:<line>: <request> -> <reply>` with the trace's line, and stops
that trace. A reply other than `ok` to the configuration (`R`, a `C` or `B`, where `bad` comes
from) prints `<file>: <request>: <reply>` without a line, because the requests are built from the
header, and stops that trace too.

**Stop and report it**: the trace, the first mismatch line as printed, and the stats line. The
first mismatch is the one to read; later ones are mostly its consequences. Never change a trace,
and add no tolerance: a board that computes a different bit than the host is the finding the test
exists for, and it is answered in the core or the build, not in the data.
