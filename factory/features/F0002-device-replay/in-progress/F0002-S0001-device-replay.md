---
stage: approval
blocked: null
round: 1
attempts: 0
crashes: 0
pull_request: 8
comments_read: []
handed_over: approval
claim: null
journal:
- number: 1
  speaker: human
  kind: created
  at: '2026-11-11T20:54:33.853109Z'
  stage: doing
- number: 2
  speaker: factory
  kind: started
  at: '2026-11-11T20:54:33.859265Z'
  stage: doing
- number: 3
  speaker: builder
  kind: result
  at: '2026-11-11T21:01:47.907926Z'
  stage: doing
  run: 9f91c9ab3b8846b2a25567c60c1c13cf
- number: 4
  speaker: factory
  kind: started
  at: '2026-11-11T21:01:54.656762Z'
  stage: review
- number: 5
  speaker: reviewer
  kind: result
  at: '2026-11-11T21:03:46.048524Z'
  stage: review
  run: 60dc19a5d3314863a70e4a920d04b5d7
- number: 6
  speaker: factory
  kind: started
  at: '2026-11-11T21:03:51.422874Z'
  stage: doing
- number: 7
  speaker: builder
  kind: result
  at: '2026-11-11T21:05:23.440370Z'
  stage: doing
  run: 41e64f99edd64d1f84a27c9d079bea79
- number: 8
  speaker: factory
  kind: started
  at: '2026-11-11T21:05:29.186099Z'
  stage: review
- number: 9
  speaker: reviewer
  kind: result
  at: '2026-11-11T21:07:00.523960Z'
  stage: review
  run: 57674cf8e3404544bdc17fee38e1b436
---

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

1. 2026-11-11 human: created.
2. factory: started doing as heishamon.
3. builder: Built the device replay: the line protocol, the test sketch with its host twin, and the replay script. Every part is proven on the host. The sketch only compiles here, because no board is attached.

   What I built:
   - `HeishaMon/src/control/wire/ctl_wire.h` and `ctl_wire.c`. This is the protocol as the Interface defines it. It is plain C99 with no dynamic memory, every loop is bounded, and it includes the core's headers by relative path. Each handler parses all of its fields before it changes the session, so an `error` reply never leaves a partial change behind. The reply is written through a bounded appender, so a small buffer is cut short and never overrun.
   - `tests/control/test_wire.c`, which covers criteria 1–5:
     - a table test for double formatting and parsing, including NaN and the inputs that must be refused;
     - the session before and after `B`, a bad configuration giving `bad 00000008`, and `R`;
     - every `C` field, the `trim_limit_enabled` flag and the refused `C` lines;
     - a twin run in which every reply is compared with a line formatted from direct core calls. It has 15 calls, including an `E` with all required signals stale (`-`), an `E` with inputs, and an `E` at NaN.
     - the malformed table, which checks that the session's bytes are unchanged afterwards (memcmp), plus extra rows for an empty line, leading or trailing spaces, `S`, `B 1`, `M .. 07` and `O .. 00`;
     - line length, a trailing `\r`, and a reply cut to its size.
     I first ran it without `ctl_wire.c` and it failed to link (20 undefined references).
   - `tests/control/device/host_server.c` (the host twin), `tests/control/device/replay/replay.ino` (the sketch: 921,600 baud, `setRxBufferSize(1024)`, static session, `micros()` timing, `ESP.getFreeHeap()`, core headers inside `extern "C"`), and `tests/control/device/replay.py` (standard library only; pyserial is imported only by the serial link).
   - Docs: `docs/control/device-test.md` is new. `docs/control/core.md` gets one sentence at the end of *How the core is proven*. `docs/control/decisions.md` gets two entries.

   Decisions (recorded in decisions.md):
   - **Carriage return before the length check:** one trailing `\r` is dropped before the line is measured against `CTL_WIRE_LINE_MAX`. A `B` that finds the configuration bad clears `begun` and leaves `calls` as it was.
   - **Sketch timing:** the time sums reset on a successful `B`, and a call's time is added only when its reply is not `error`.
   - **The script reads a whole trace before sending any of it:** a malformed trace prints `<file>:<line>: format error` and nothing of it is sent. This lets the script keep 16 requests in flight. I rejected reading line by line like the C runner.
   - **Script output and exits:**
     - a reply that is no answer prints `<file>:<line>: <request> -> <reply>`;
     - mismatch values are spelled the way the trace spells them (repr, enum names, signal sets, `none`);
     - the script waits for `ready` from the host twin too;
     - it exits with 2 when the link cannot be opened (no pyserial, or no such port).

   Verification:
   - I ran the script over every file in `tests/control/data/bad/`. It reports a format error at the same lines as the C runner for the malformed ones, passes the `match_*` and `valid_small` files, and finds the mismatches in the `mismatch_*` and `wrong_*` files. These runs are by hand, not part of `make test`.
   - All 13 traces through the host twin: `13 files, 332239 calls, 0 mismatches`, in 2.8 s.
   - I checked by hand that a file stops at 10 mismatches, and that the next file still passes after replies still in flight were drained.

   Smoke test, run as written:
   1. First line `tests/control/data/bad/wrong_energy.trace:12: E: expected -28.708333333333332, got -28.708333333333336`, then `exit 1`. Matches.
   2. `exit 2`. Matches.
   3. `make device | grep -c '^\. '` printed `2`. Matches.

   `make check` is green: test prints `device replay: 2 files, 10639 calls, 0 mismatches`; device uses 29,280 bytes of RAM and 242,980 bytes of flash.

   Noticed but not touched:
   - Criterion 5's test cannot tell from the reply alone that a 128-character line was refused for its content rather than its length, because no valid request is that long. It checks `error` and also that the same line with a `\r` added is still handled.
   - With a `\r\n` line end, the host twin's buffer of `CTL_WIRE_LINE_MAX + 2` (as the story specifies) refuses a 128-character line. The sketch's buffer has room for it.
   - A second arduino-cli compile, to read the warnings from our own files, was denied permission, so warnings from `ctl_wire.c` on the board's compiler are unchecked. `make device` compiles and is green.
   - pyserial is not installed on this machine, so the `--port` path ran only as far as its exit 2.
   - The flashing commands in `device-test.md` use the standard ESP8266 index URL (`https://arduino.esp8266.com/stable/package_esp8266com_index.json`), not one taken from the factory image.

   `check`: check: green
4. factory: started review as heishamon.
5. reviewer: Findings:

   1. `tests/control/device/replay.py`, `replay_calls`: when a file stops (10 mismatches, or a reply `error`/`bad`/wrong field count) while calls are still unsent, `pending` drains to empty while `next_call < len(calls)` keeps the loop running, and `pending.popleft()` raises IndexError. The script then dies with a traceback: no per-file line, no next file, no `S`, no summary. Every trace in `tests/control/data/bad/` has at most 12 calls, which is under IN_FLIGHT, so the by-hand checks never reached this path. On the board it would hit the first real trace that stops. Fix: once `stopped`, end the loop when `pending` is empty, e.g. `while (not stopped and next_call < len(calls)) or pending`.
   2. `tests/control/test_wire.c`, `test_replies_equal_core` / `test_malformed`: the demand-mask limit is tested only on its refused side (`8`, `g`). The `M ... 7` in `test_session_before_begin` runs before `B`, so it replies `error`. The accepted side, mask 7 (`CTL_DEMAND_ALL`), is never answered with a state line. Likewise no `O` uses signal 4 (`P_INTERNAL`). Fix: add a `twin_allow(..., CTL_DEMAND_ALL)` and a `twin_observe(..., CTL_SIGNAL_P_INTERNAL, ...)` to the twin run.
   3. `tests/control/device/replay.py`, `SerialLink.wait_ready`: the deadline is checked only before each `readline`, and each read can block for REPLY_TIMEOUT_S (10 s). The wait for `ready` can therefore last almost 20 s, but the Interface says at most 10 seconds. Fix: set the port's timeout to the time left before each read, or read with a short timeout in the loop.
   4. `tests/control/device/replay.py`, `replay_file`: a trace that cannot be opened or is malformed returns before the line `<file>: <calls> calls, <mismatches> mismatches`, which the Interface requires after each file. Fix: print it, with 0 calls, on those paths too.
   5. `docs/control/device-test.md`, *On a mismatch*: it says an `error` or `bad` reply "prints the trace's line with the request and the reply". `configure` in `replay.py` prints `<file>: <request>: <reply>` with no trace line. Fix: make the document match the code, or carry the header line number into the config requests and print it.
   6. `docs/control/decisions.md`, entry "The wire protocol ignores a carriage return before it measures the line": it holds three decisions under one title (the `\r` before the length check, a bad `B` ending `begun` while keeping `calls`, and the sketch's timing sums), but its reason and rejected alternative cover only the `\r`. CLAUDE.md asks for one entry per decision, each with its own reason and what was rejected. Fix: split it into three entries.
   7. `tests/control/test_wire.c`, `check_config_after_begin`: this is a test, but it carries the `check_` prefix that the file uses for helpers (`check_same_config`). Fix: rename it `test_config_after_begin`.
   8. `tests/control/test_wire.c`, `decision_line`: `values[9]` (`0.0`) and `numbers[9]` are declared but never used. This is dead code. Fix: size both arrays to 9.

   Earlier tests: none. `git diff --name-status develop...HEAD` shows every test file as `A`.

   Checked and found sound:
   - Criteria 1, 3, 4, 5 and 8 have tests that match them. Every `C` row sets a value other than its default.
   - The bounds and buffer sizes in `ctl_wire.c` are correct, and an `error` reply leaves the session unchanged.
   - The core has no loop whose bound comes from the configuration, so a `C` after `B` cannot hang the board.
   - The architecture rules hold, and no upstream file is touched.
   - The factory's `make check` after the builder was green.
6. factory: started doing as heishamon.
7. builder: Round 1: fixed the reviewer's eight findings and changed nothing else.

   1. `tests/control/device/replay.py`, `replay_calls`: the loop now runs `while (not stopped and next_call < len(calls)) or pending`. Before, a file that stopped with calls not yet sent would empty `pending` and the script crashed in `popleft`. I tested this by hand with the host twin. I made a copy of `edge_cases.trace` with the recorded `E` of every `evaluate` in its first 400 lines set to 12345.0, then ran it followed by `bad/unknown_call.trace` and `bad/valid_small.trace`. The copy stopped after 10 mismatches (84 calls), the next two files ran, and the `S` line, the summary and exit 1 all followed. Earlier this path raised IndexError. The `error`/wrong-field-count stop uses the same loop and was not run separately: no valid trace makes the twin reply `error`.
   2. `tests/control/test_wire.c`, `test_replies_equal_core`: added `twin_observe(..., CTL_SIGNAL_P_INTERNAL, 150.0)` at 260 s and `twin_allow(..., CTL_DEMAND_ALL)` at 270 s, so the accepted sides of signal 4 and mask 7 are now answered with state lines and compared with direct core calls. The expected `calls` count went from 15 to 17.
   3. `replay.py`, `SerialLink.wait_ready`: before each read, the port's timeout is set to the time left before the deadline, and the reply timeout is restored afterwards (`finally`). The wait now lasts at most 10 s. This was not run, because pyserial and a board are missing here.
   4. `replay.py`, `replay_file`: a file that cannot be opened or that has a format error now also prints `<file>: 0 calls, 0 mismatches`. Seen in the run above for `unknown_call.trace`.
   5. `docs/control/device-test.md`, *On a mismatch*: I changed the document to match the code. A bad reply to a call prints `<file>:<line>: <request> -> <reply>`. A reply other than `ok` to `R`/`C`/`B` (which is where `bad` comes from) prints `<file>: <request>: <reply>` with no line number, because those requests are built from the header. I chose not to carry header line numbers into the configuration requests, since the document only needed to describe what happens.
   6. `docs/control/decisions.md`: split the combined entry into three, each with its own reason and rejected alternative: the `\r` before the length check; a bad `B` ends the begun state and keeps `calls`; the sketch times only the calls that `calls` counts.
   7. `test_wire.c`: renamed `check_config_after_begin` to `test_config_after_begin`.
   8. `test_wire.c`, `decision_line`: `numbers` and `values` now have 9 entries; the unused tenth entry is gone.

   Smoke test, run as written:
   1. The first line is `tests/control/data/bad/wrong_energy.trace:12: E: expected -28.708333333333332, got -28.708333333333336`, then `exit 1`. Matches.
   2. `exit 2`. Matches.
   3. Permission was denied when I chained the `make device | grep -c` pipe with other commands, and I did not run it separately. The `make check` output shows exactly two lines starting with `. ` (RAM 29,280 bytes, flash 242,980 bytes), which is what the pipe counts.

   `make check` is green. The test target prints `device replay: 2 files, 10639 calls, 0 mismatches`.

   Noticed but not touched: the earlier round's remarks still hold. pyserial is not installed, so `--port` is unexercised. The board compiler's warnings on `ctl_wire.c` are unread.

   `check`: check: green
8. factory: started review as heishamon.
9. reviewer: no findings

   I read the diff again in full, with all eight findings from the earlier round checked against the code:
   1. `replay.py` `replay_calls` now loops on `(not stopped and next_call < len(calls)) or pending`. A file that stops drains the replies still in flight and ends without calling `popleft` on an empty deque.
   2. `test_wire.c` `test_replies_equal_core` now answers signal 4 (`P_INTERNAL`) and mask 7 (`CTL_DEMAND_ALL`) with state lines and compares both with direct core calls. The expected `calls` is 17.
   3. `SerialLink.wait_ready` now gives each read only the time left before the 10-second deadline, and restores the reply timeout in `finally`.
   4. `replay_file` prints `<file>: <calls> calls, <mismatches> mismatches` on every path, including when the file cannot be opened or has a format error.
   5. *On a mismatch* in `device-test.md` now describes both outputs as `replay.py` prints them: `<file>:<line>: <request> -> <reply>` for a call, and `<file>: <request>: <reply>` for the configuration.
   6. `decisions.md` now has three separate entries, each with its own reason and rejected alternative: the `\r` before the length check, a bad `B` keeping `calls`, and the sketch's timing.
   7. The test is renamed `test_config_after_begin`.
   8. `decision_line` now uses arrays of 9; nothing is left unused.

   Checked again and found sound:
   - Criteria 1–5 have table tests that match them. Criteria 6–8 are covered by the Makefile, which this story does not change, and by the smoke tests.
   - The bounds in `ctl_wire.c`: `bounded_length` reads at most 130 characters, `split` is bounded, and the reply appender never writes past `reply_size`.
   - The architecture rules hold: `wire/` is plain C99, includes the core by relative path and uses no dynamic memory. No upstream file is touched.
   - The sketch's timing overflows only past about 71 minutes of total call time (`uint32_t`), well beyond the largest trace.
   - The docs' claims match the code.
   - The factory's `make check` after the builder was green (journal entry 7).

   Earlier tests: none. Every test file in `git diff --name-status develop...HEAD` has status `A`.

   Notes:
   - `tests/control/device/host_server.c` `read_line`: `fgets` with a 130-byte buffer refuses a 128-character line ending in `\r\n`, while the sketch's character-by-character reader keeps it. This cannot change a reply, because no valid request comes near 128 characters (the longest is about 41), so both sides answer `error` either way.
   - `tests/control/test_wire.c` `test_line_length`: as the builder noted, the accepted side of `CTL_WIRE_LINE_MAX` cannot be seen in a reply, because no valid request is that long. Criterion 5's "handled for its content, not its length" can only be checked indirectly.
