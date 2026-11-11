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
| the frame | `HeishaMon/src/control/wire/ctl_frame.h`, `ctl_frame.c` | adds the checksum frame to a line and checks and cuts it off; plain C too |
| the test sketch | `tests/control/device/replay/replay.ino` | moves lines between the USB serial port and the protocol, and measures the time each call takes |
| the host twin | `tests/control/device/host_server.c` | does the same over standard input and output on the host |
| the replay script | `tests/control/device/replay.py` | reads the traces, sends their configuration and calls to the sketch or the twin, compares every reply, and restarts a trace after a broken line |
| the script's tests | `tests/control/device/test_replay.py`, `garbling_twin.py` | prove the twin's frames and the script's restarts, reset and wait for `ready`; `make test` runs them |

`make test` builds the host twin and replays `edge_cases` and `flaky_transport` through it; it
prints `device replay: 2 files, 10639 calls, 0 mismatches, 0 restarts`. `make device` compiles the sketch for
the D1 mini and prints the memory it uses. Neither touches a board: flashing it and replaying all
thirteen traces is a step by hand, below.

## Why doubles cross the line as bit patterns

Every double travels as the 16 lowercase hexadecimal digits of its IEEE-754 bit pattern, most
significant first: `1.0` is `3ff0000000000000`, `-0.0` is `8000000000000000`, a NaN is
`7ff8000000000000`. Decimal text would have to be printed by the board's `printf` and read back by
Python, two conversions that could round or spell a value differently and hide or invent a
difference. The bit pattern carries the value exactly, NaN and the infinities and the sign of zero
included, and needs no floating-point code on the board to write it.

## The frame

Every line in both directions, `ready` and the stats line included, carries a frame: the text, a
space, `*`, and the checksum of the text, the XOR of all its bytes as two lowercase hexadecimal
digits. `ok` travels as `ok *04`, because `'o'` is `0x6f` and `'k'` is `0x6b`; the empty text as
` *00`. A trailing `\r` is ignored before the frame is checked. A frame checks only when it ends
the line exactly, with lowercase digits that match: `ok *05`, `ok *0`, `ok *04x`, `ok*04` and
`ok *0A` are all broken.

A serial line at high speed can drop or repeat a character, and a dropped digit in a double would
otherwise read as a mismatch of the core, or as no reply at all. The sketch and the twin cut the
frame off every request before anything else; a request whose frame does not check is answered
with `error` (framed) and reaches neither the protocol nor the stats. The script checks the frame
of every reply.

## The protocol

The lines below are the texts inside the frame. One request per line; fields are separated by
exactly one space. A request text is at most 128 characters (`CTL_WIRE_LINE_MAX`), without its
frame and line end. Every request gets exactly one reply line.

- Doubles: 16 lowercase hexadecimal digits, as above. Fewer or more digits, uppercase letters and
  any other character are refused.
- Enums: their C values as decimal digits. `ctl_demand`: `HEATING` 0, `COOLING` 1, `NONE` 2;
  `ctl_reason`, `ctl_phase` and `ctl_signal` in their declared order (`core.md`).
- Masks: hexadecimal. A demand mask is one digit (`CTL_DEMAND_BIT` of each permitted demand), a
  signal mask two digits (`CTL_SIGNAL_BIT`), the configuration's problem bits eight digits.

| Request | Effect | Reply |
|---|---|---|
| `R *52` | `ctl_config_defaults`, `ctl_state_init`, not begun, `calls` 0 | `ok *04` |
| `C <name> <value> *<xx>` | sets one field of the configuration; `<name>` is a field of `ctl_config`, or `max_age_s.<i>` / `fallback_w.<i>` with `<i>` the signal's value 0 to 4; `trim_limit_enabled` takes `0` or `1`, every other field a double | `ok *04` |
| `B *42` | validates the configuration; when valid, `ctl_state_init`, begun, `calls` 0 | `ok *04`, or `bad <problem bits> *<xx>` and not begun |
| `O <t> <signal> <value> *<xx>` | `ctl_observe` | the state line, framed |
| `A <t> *<xx>` | `ctl_advance` | the state line, framed |
| `M <t> <demand mask> *<xx>` | `ctl_set_allowed` | the state line, framed |
| `E <t> *<xx>` | `ctl_evaluate` | the decision line, framed |
| `S *53` | answered by the sketch or the twin, not by the protocol | the stats line, framed |

`<xx>` is the checksum of the text before it.

- The state line is `<energy_wh> <integrated_s> <skipped_s> <clamped>`, with `clamped` as `0` or
  `1`.
- The decision line is `<demand> <reason> <phase> <energy_wh> <dt_s> <integrated_s> <skipped_s>
  <clamped> <p_net_w> <unhealthy> <substituted> <produced_wh> <solar_wh> <internal_wh>
  <transmission_wh> <trim_wh>`, with `p_net_w` as `-` when the decision has no inputs.
- Any other line gets `error` and changes nothing: a line whose frame does not check, an unknown request, a missing or a surplus
  field, a field that does not parse, a signal above 4, a demand mask above 7, an unknown
  configuration name, `O`, `A`, `M` or `E` before a successful `B`, and a text longer than 128
  characters.
- `C` after `B` is accepted and ends nothing; the next `B` validates and starts again.
- The stats line is `stats calls=<n> us_total=<n> us_max=<n> heap=<n> state_bytes=<n>`: the
  `O`, `A`, `M` and `E` requests answered since the last `B`; the microseconds the board spent in
  `ctl_wire_handle` for them, summed and the largest; the free heap in bytes; and the size of the
  session (configuration and state) in bytes. The host twin writes 0 for the time and the heap.

The sketch and the twin print `ready *6b` once when they can take requests. The sketch runs the
serial port at 230,400 baud with a receive buffer of 1,024 bytes; the script keeps 4 requests in
flight. At 921,600 baud the owner's D1 mini dropped and repeated characters after some 26,000
calls; at 230,400 baud with 4 requests in flight it replayed all thirteen traces.

## Restarts

A broken line, an `error` reply, a reply with the wrong number of fields or a field that does not
parse, and no reply within 10 seconds each stop the current trace. While the configuration is
sent, every reply to `R`, a `C` or `B` other than `ok` is a break too, a stale line from an
earlier attempt included; only `bad …` to `B` fails the trace without a restart (below). The
script prints `<file>:<line>: broken line, restarting (<k> of 2): <what came>`, with the trace's
line of the call (without `:<line>` when it broke while sending the configuration), reads and
drops replies for 1 second, and starts the trace again from its beginning: `R`, its `C` lines,
`B` and every call. `<what came>` is the broken line in the safe form below,
`<request> -> <reply>` for a reply that is no answer, or `no reply within 10 s`. A third break
prints `<file>:<line>: broken line: <what came>`, then `<file>: failed after 2 restarts`, and the
trace fails. A restarted trace counts only its last attempt's calls and mismatches.

A call is never sent again on its own. Every call changes the core's state: an `O` stores a
value, an `A` integrates, an `E` moves the balance, and a call that was lost on the way in, or
whose reply was lost on the way out, may or may not have run. Only a replay from `R` puts the core
back into a state the trace knows.

Everything the script prints from the link is ASCII: every byte outside the printable range is
written as `\xNN`, so that a garbled reply cannot crash a console that cannot print it.

The host twin proves this without a board: `host_server --garble <n>` replaces the first
character of its `n`-th reply after `ready` with `#` and keeps the old checksum. Any other
argument than none or `--garble` and one to nine decimal digits (a sign and a longer number
included) prints `usage: host_server [--garble <n>]` and exits with 2; `--garble 0` garbles
nothing. `tests/control/device/garbling_twin.py` runs the host twin and breaks the replies it is
given by number (`--at`, repeatable), or every reply, in the other ways: a framed `error`, the wrong number of fields, a field that does
not parse, no reply at all, or a broken frame with a chosen byte. `test_replay.py` replays
`edge_cases` with each break once (one restart, 0 mismatches), with a break in each of
its first two attempts (2 restarts, and the trace passes), and with every reply broken (the
trace fails after 2 restarts, the broken bytes printed safely), and replays `edge_cases` and
`flaky_transport` through the plain twin with 0 restarts.

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

It unpacks the thirteen traces and opens the port at 230,400 baud. It then resets the board
itself: DTR off, RTS on for 0.2 seconds, RTS off; on a D1 mini RTS pulls the reset line, and DTR
off lets it boot the sketch rather than the loader. Not every port resets the board when it
opens, so the script does not rely on it; the owner's D1 mini (FTDI USB, ESP-12F) needs no button
press. The script waits up to 10 seconds for a framed `ready`, ignoring every other line, the
board's boot messages included. It then replays each trace and prints one line per trace, the
stats line and a summary:

```
.../barbarastr_march.trace: 129493 calls, 0 mismatches
...
.../winter_day.trace: 25912 calls, 0 mismatches
stats calls=25912 us_total=... us_max=... heap=... state_bytes=...
device replay: 13 files, 332239 calls, 0 mismatches, 0 restarts
```

The stats line covers the last trace only: `us_total / calls` is the mean time of one call on the
board, `us_max` the slowest, `heap` what the sketch leaves free, and `state_bytes` what one
controller's configuration and state take. The summary counts the calls and mismatches of each
trace's last attempt, and the restarts of all traces. The script exits with 0 when every trace
passed, however many restarts it took, 1 when one did not, and 2 on a wrong command line, when the port cannot be opened or when no
`ready` came.

## On a mismatch

A value that differs prints `<file>:<line>: <key>: expected <value>, got <value>`, with the
trace's line and key; a trace stops after ten. A broken reply restarts the trace (above); a
restart is the link, not the core, and a trace that passes after one is a pass. A `bad` reply to
`B` prints `<file>: B: <reply>` without a line, because the requests are built from the header,
and stops that trace without a restart: the same configuration would be bad again.

**Stop and report it**: the trace, the first mismatch line as printed, and the stats line. The
first mismatch is the one to read; later ones are mostly its consequences. Never change a trace,
and add no tolerance: a board that computes a different bit than the host is the finding the test
exists for, and it is answered in the core or the build, not in the data.

**Known limit: a line that vanishes whole.** With 4 requests in flight, the script pairs each
reply with the oldest request still waiting. A reply the link loses whole, newline and all,
breaks no frame, so it is not caught: every reply after it is read as the answer to the call
before its own. When the calls around it are of the same kind, the field counts still match, and
the trace shows mismatches from one line on rather than a restart, and it mostly stops after ten
of them before the last request's missing reply would time out. A dropped newline alone merges two lines and breaks the frame, so only a whole line
does this. The symptom is mismatches from one line on that do not repeat when the trace is
replayed again. **Before believing a mismatch, replay that trace again**: a mismatch from the
core comes back at the same line every time, one from a vanished line does not. The frame carries
no sequence number, so that its format stays the text, ` *` and the checksum.
