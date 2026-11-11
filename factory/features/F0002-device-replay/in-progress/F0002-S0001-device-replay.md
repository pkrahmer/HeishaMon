# The device replay: a line protocol, a test sketch and a replay script

## Description

The owner wants the golden traces replayed through the C core on a D1 mini (ESP8266), to prove that the board's compiler computes every value exactly as the host does. This story builds the three parts that make it possible, and proves them on the host:

- a line protocol in C under `HeishaMon/src/control/wire/`, which turns one request line into calls on the core and writes one reply line, with doubles as their exact bit patterns;
- the test sketch `tests/control/device/replay/replay.ino`, which moves lines between the USB serial port and that protocol and measures the time each call takes, and its host twin `tests/control/device/host_server.c`, which does the same over standard input and output;
- the script `tests/control/device/replay.py`, which reads golden traces, sends their configuration and calls to the sketch or the host twin, and compares every reply with the recorded values exactly.

`make test` builds the host twin and replays two traces through it; `make device` compiles the sketch for the D1 mini. Flashing the board and running all thirteen traces on it is the owner's step after the merge, with `make device-replay PORT=<port>`.

The story changes nothing in the core and nothing in the trace runner of the earlier feature.

## Interface

New files: `HeishaMon/src/control/wire/ctl_wire.h`, `ctl_wire.c`; `tests/control/test_wire.c`; `tests/control/device/host_server.c`, `tests/control/device/replay/replay.ino`, `tests/control/device/replay.py`; `docs/control/device-test.md`. The `.c` files of `wire/` include the core's headers by a relative path (`"../core/ctl_controller.h"`), because `make device` compiles each folder under `HeishaMon/src/control/` as a flat library.

```c
/* ctl_wire.h */
#define CTL_WIRE_LINE_MAX 128   /* the longest request line, without its line end */
#define CTL_WIRE_REPLY_SIZE 256 /* the buffer a reply needs, with its terminating zero */

typedef struct {
    ctl_config config;
    ctl_state state;
    bool begun;
    uint32_t calls;  /* O, A, M and E requests answered since the last B */
} ctl_wire_session;

void ctl_wire_init(ctl_wire_session *session);
void ctl_wire_handle(ctl_wire_session *session, const char *line, char *reply, size_t reply_size);
void ctl_wire_format_double(double value, char *text);       /* writes 16 digits and a zero */
bool ctl_wire_parse_double(const char *text, double *value);  /* exactly 16 digits, nothing after */
```

Doubles on the line: 16 lowercase hexadecimal digits of the IEEE-754 bit pattern, most significant first (`1.0` is `3ff0000000000000`). Parsing refuses fewer or more digits, uppercase letters and any other character. Enums are their C values as decimal digits (`ctl_demand`: `HEATING` 0, `COOLING` 1, `NONE` 2; the others in their declared order). Masks are hexadecimal: a demand mask one digit, a signal mask two digits, the configuration's problem bits eight digits.

The requests, one per line. `line` holds no line end; a trailing `\r` is ignored. Fields are separated by exactly one space.

| Request | Effect | Reply |
|---|---|---|
| `R` | `ctl_config_defaults`, `ctl_state_init`, not begun, `calls` 0 | `ok` |
| `C <name> <value>` | sets one field of the configuration; `<name>` is a field of `ctl_config`, or `max_age_s.<i>` / `fallback_w.<i>` with `<i>` the signal's value 0 to 4; `trim_limit_enabled` takes `0` or `1`, every other field a double | `ok` |
| `B` | validates the configuration; when valid, `ctl_state_init`, begun, `calls` 0 | `ok`, or `bad <problem bits>` and not begun |
| `O <t> <signal> <value>` | `ctl_observe` | the state line |
| `A <t>` | `ctl_advance` | the state line |
| `M <t> <demand mask>` | `ctl_set_allowed` | the state line |
| `E <t>` | `ctl_evaluate` | the decision line |

- The state line is `<energy_wh> <integrated_s> <skipped_s> <clamped>`, with `clamped` as `0` or `1`.
- The decision line is `<demand> <reason> <phase> <energy_wh> <dt_s> <integrated_s> <skipped_s> <clamped> <p_net_w> <unhealthy> <substituted> <produced_wh> <solar_wh> <internal_wh> <transmission_wh> <trim_wh>`, with `p_net_w` as `-` when the decision has no inputs.
- Any other line gets the reply `error` and changes nothing: an unknown request, a missing or a surplus field, a field that does not parse, a signal above 4, a demand mask above 7, an unknown configuration name, `O`, `A`, `M` or `E` before a successful `B`, and a line longer than `CTL_WIRE_LINE_MAX`.
- `C` after `B` is accepted and ends nothing; the next `B` validates and starts again.

The sketch and the host twin:

- Both print `ready` once when they can take requests, and then answer each line with exactly one line.
- Both answer `S` themselves with `stats calls=<n> us_total=<n> us_max=<n> heap=<n> state_bytes=<n>`: `calls` from the session; `us_total` and `us_max` the microseconds spent in `ctl_wire_handle` for `O`, `A`, `M` and `E` since the last `B`, summed and the largest; `heap` the free heap; `state_bytes` `sizeof(ctl_wire_session)`. The host twin writes 0 for the time and the heap.
- The sketch runs the serial port at 921,600 baud with a receive buffer of at least 1,024 bytes, and holds the session in a static variable. The host twin reads standard input with a buffer of `CTL_WIRE_LINE_MAX + 2` bytes, refuses a longer line with `error`, and flushes after each reply.

The script `replay.py`, standard library only except `serial` (pyserial), imported only for `--port`:

- `replay.py --command <program> <trace>...` starts the program and talks to it through its standard input and output. `replay.py --port <port> <trace>...` opens the serial port at 921,600 baud and reads lines until `ready`, ignoring every other line, for at most 10 seconds (the board resets when the port opens).
- For each trace it reads the format of `docs/control/golden-traces.md` and refuses a malformed file as the trace runner does. It sends `R`, one `C` per header line, and `B`. `trim_limit_k none` becomes `C trim_limit_enabled 0`; a number becomes `C trim_limit_enabled 1` and `C trim_limit_k <value>`. `state_max_age_s` and `persist_interval_s` are not sent.
- It then sends each call and compares the reply with the recorded values exactly: numbers equal by value, or both NaN, so `-0.0` equals `0.0`; enums, masks and `pnet` as recorded. It keeps up to 16 requests in flight.
- A value that differs prints `<file>:<line>: <key>: expected <value>, got <value>` and counts one mismatch; after 10 mismatches it stops that file. A reply `error` or `bad`, or a reply with the wrong number of fields, prints the line and stops that file as a failure.
- After each file it prints `<file>: <calls> calls, <mismatches> mismatches`; at the end it sends `S`, prints the reply, and prints `device replay: <files> files, <calls> calls, <mismatches> mismatches`. It exits with 0 when every file passed, 1 otherwise, and 2 on a wrong command line or when no `ready` came.

## Acceptance criteria

1. `ctl_wire_format_double` and `ctl_wire_parse_double`, as rows of one table test: `0.0` is `0000000000000000`, `-0.0` is `8000000000000000`, `1.0` is `3ff0000000000000`, `-2.5` is `c004000000000000`, `INFINITY` is `7ff0000000000000`, and the next double above 1.0 is `3ff0000000000001`; each parses back to the same bit pattern, and `7ff8000000000000` parses to a NaN. Parsing refuses 15 digits, 17 digits, `3FF0000000000000`, a `g`, and the empty text.
2. The session: after `ctl_wire_init`, `O`, `A`, `M` and `E` each reply `error`. `R` replies `ok`. `B` with the defaults replies `ok`; after `C hysteresis_wh 0000000000000000` it replies `bad 00000008` and the session is not begun.
3. `C` sets each field: a table test sends one `C` for every field of `ctl_config`, five entries of `max_age_s` and five of `fallback_w`, each with a value other than the default, and compares the session's configuration field by field. `C trim_limit_enabled 0` and `1` set the flag. An unknown name, `max_age_s.5`, `trim_limit_enabled 2`, and a value of 15 digits each reply `error` and leave the configuration as it was.
4. The replies equal the core's own results: a test runs one sequence of requests through the session (the three readings of the controller story, then `A`, `M`, `E` at several instants, including an `E` with every required signal stale) and the same calls directly on a second state with the same configuration, and compares each reply with the line formatted from the direct results. One `E` reply has `-` for `p_net_w`, and one has a number.
5. Malformed requests, as rows of one table test, each reply `error` and leave the session's state equal to a copy taken before: `X`, `A`, `A 3ff0000000000000 1`, `O 3ff0000000000000 5 3ff0000000000000`, `M 3ff0000000000000 8`, `M 3ff0000000000000 g`, `E 3FF0000000000000`, a line of `CTL_WIRE_LINE_MAX + 1` characters, and `A` with two spaces before its field. A line of exactly `CTL_WIRE_LINE_MAX` characters is handled (it replies `error` for its content, not for its length), and a trailing `\r` is ignored.
6. `make test` builds the host twin and runs `replay.py` over `edge_cases.trace` and `flaky_transport.trace`; it prints `device replay: 2 files, 10639 calls, 0 mismatches` and the target stays green.
7. The script finds faults, shown by the smoke test: `replay.py --command <host twin> tests/control/data/bad/wrong_energy.trace` exits with 1 and its first mismatch line names line 12 and the key `E`; `replay.py` without `--command` or `--port` exits with 2.
8. `make device` compiles the sketch for the D1 mini and prints its memory lines; the sketch includes `ctl_wire.h` and the core's headers it needs inside `extern "C"`.
9. `make check` stays green.
10. docs: `docs/control/device-test.md` is new. It says what the test proves and why the doubles cross the line as bit patterns; the protocol as the table above; how to flash the D1 mini from a PC with arduino-cli (the core's index URL, the core version 3.1.2, the board `esp8266:esp8266:d1_mini`, the compile with each folder of `HeishaMon/src/control/` as `--library`, and `arduino-cli upload -p <port>`); how to run `make device-replay PORT=<port>` and read its output and the stats line; and what to do on a mismatch: stop and report it with the trace's line, never change a trace. `docs/control/core.md` gets one sentence under *How the core is proven* that points to it.

## Smoke test

```sh
d=$(mktemp -d); trap 'rm -rf "$d"' EXIT; gcc -std=c99 -ffp-contract=off -IHeishaMon/src/control tests/control/device/host_server.c $(find HeishaMon/src/control -name '*.c') -lm -o "$d/server"; python3 tests/control/device/replay.py --command "$d/server" tests/control/data/bad/wrong_energy.trace | head -1; python3 tests/control/device/replay.py --command "$d/server" tests/control/data/bad/wrong_energy.trace > /dev/null; echo "exit $?"
```

Expect: two lines: first a mismatch line that starts with `tests/control/data/bad/wrong_energy.trace:12: E: expected`, then `exit 1`

```sh
python3 tests/control/device/replay.py tests/control/data/bad/valid_small.trace > /dev/null 2>&1; echo "exit $?"
```

Expect: `exit 2`

```sh
make device | grep -c '^\. '
```

Expect: `2`

## Journal
