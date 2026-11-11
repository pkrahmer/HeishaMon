---
stage: doing
blocked: null
round: 0
attempts: 0
crashes: 0
pull_request: null
comments_read: []
handed_over: null
claim:
  instance: heishamon
  stage: doing
  started_at: '2026-11-11T21:38:37.026433Z'
  lease_duration: 60
journal:
- number: 1
  speaker: human
  kind: created
  at: '2026-11-11T21:38:37.020595Z'
  stage: doing
- number: 2
  speaker: factory
  kind: started
  at: '2026-11-11T21:38:37.026433Z'
  stage: doing
---

# A checksum on every line, a restart after a broken one, a board reset and 230,400 baud

## Description

The device replay as merged fails on the owner's D1 mini in three ways: the board does not reset when its serial port opens, so the script never sees `ready`; at 921,600 baud the line drops and repeats characters after about 26,000 calls, which ends the run; and printing a garbled reply crashes the script on the Windows console. A diagnostic run with a reset pulse, 230,400 baud and 4 requests in flight passed all thirteen traces, so the core is sound and only the tooling needs this fix.

The story puts a frame around every line in both directions: the text, a space, `*` and a checksum. The sketch and the host twin check the frame of each request before they hand it to `ctl_wire_handle`, refuse a broken one, and frame every reply. The script frames every request and checks every reply. A broken reply, a refused request or a reply that never comes restarts the trace from its beginning, at most twice. A call is never sent twice within one replay, because a call changes the core's state.

The script also resets the board after it opens the port, and both sides run at 230,400 baud with 4 requests in flight. Every line the script prints is safe on any console. The host twin gets an option that garbles one reply on purpose, so that `make test` proves the restart without a board.

The meaning of the requests in `ctl_wire.c` does not change, nor does the core.

## Interface

New files: `HeishaMon/src/control/wire/ctl_frame.h`, `ctl_frame.c`, `tests/control/test_frame.c`. Changed: `tests/control/device/replay/replay.ino`, `tests/control/device/host_server.c`, `tests/control/device/replay.py`, `docs/control/device-test.md`. The `Makefile` is not changed: it runs the host replay as `host_server` with no option.

```c
/* ctl_frame.h */
#define CTL_FRAME_SUFFIX 4 /* " *" and two digits */

uint8_t ctl_frame_checksum(const char *text, size_t length);
bool ctl_frame_add(char *line, size_t size);
bool ctl_frame_strip(char *line);
```

The frame:

- The checksum of a text is the XOR of all its bytes. It is written as two lowercase hexadecimal digits.
- A framed line is the text, then ` *`, then the checksum of the text. For example, `ok` is framed as `ok *04`, because `'o'` is `0x6f` and `'k'` is `0x6b`.
- `ctl_frame_add` appends the frame to the zero-terminated `line` in place. It returns false and leaves `line` as it was when `size` cannot hold the result with its terminating zero.
- `ctl_frame_strip` checks a framed line in place. A line that ends in ` *` and two lowercase hex digits matching the checksum of the text before them gets the frame cut off, and the function returns true. A trailing `\r` is ignored first. It returns false for anything else, and then leaves `line` unchanged: no frame, a wrong checksum, uppercase digits, one digit, or text after the digits.

The sketch and the host twin:

- Each frames `ready` and every reply, `stats` included.
- Each strips the frame of every request before anything else. A request whose frame does not check is answered with a framed `error`, and it reaches neither `ctl_wire_handle` nor the stats.
- `S` is recognised after stripping.
- A line too long for the buffer is answered with a framed `error`, as before.
- The receive buffer grows by `CTL_FRAME_SUFFIX` so that a framed line of `CTL_WIRE_LINE_MAX` characters fits.
- The sketch runs at 230,400 baud.

The host twin gets `--garble <n>`:

- With it, the twin flips one character of the frame-checked text of its `n`-th reply after `ready`. Counting from 1, it replaces the first character with `#`, keeps the old checksum, and sends that broken line.
- With `--garble 0`, without the option, or with another value than a positive integer, it garbles nothing.
- A wrong argument prints `usage: host_server [--garble <n>]` to standard error and exits with 2.

The script:

- `--command` is split into a program and its arguments as a POSIX shell would split it (`shlex.split`), so that `--command "<host twin> --garble 500"` works.
- It frames every request and checks the frame of every reply. A reply whose frame does not check is a broken line.
- **Restarts.** A broken line, an `error` reply, a reply with the wrong number of fields, and no reply within 10 seconds each stop the replay of the current trace. The script prints `<file>:<line>: broken line, restarting (<k> of 2)` with the line of the call, using the safe form below. It then reads and drops replies for 1 second and starts the trace again with `R`, its `C` lines and `B`.
- **Results.** A restarted trace counts only its last attempt's calls and mismatches. A third break ends that trace as failed with `<file>: failed after 2 restarts`.
- **Reset.** With `--port`, after it opens the port at 230,400 baud, the script sets DTR off and RTS on, waits 0.2 s and sets RTS off. Then it waits up to 10 s for a framed `ready`, ignoring every other line.
- It keeps 4 requests in flight.
- **Safe printing.** Every text it prints from the link is ASCII, with any other byte written as `\xNN`.
- **Summary.** The last line is `device replay: <files> files, <calls> calls, <mismatches> mismatches, <restarts> restarts`. It exits with 0 when every file passed, whatever the number of restarts, with 1 otherwise, and with 2 as before.

The earlier tests: `tests/control/test_wire.c` is not changed, because `ctl_wire_handle` is not. `docs/control/device-test.md` changes by this story's docs criterion.

## Acceptance criteria

1. `ctl_frame_checksum` of the empty text is 0, of `ok` is `0x04`, and of `A 3ff0000000000000` is the XOR its test computes byte by byte.
2. `ctl_frame_add`, as rows of one table test:
   - `ok` becomes `ok *04`.
   - The empty text becomes ` *00`.
   - A buffer exactly one byte too small for the result returns false and leaves the text unchanged.
   - A buffer of exactly the right size returns true.
3. `ctl_frame_strip`, as rows of one table test:
   - `ok *04` and `ok *04\r` become `ok`, returning true.
   - Each of these returns false and leaves the line unchanged: `ok *05`, `ok *0`, `ok *04x`, `ok*04`, `ok *G4`, `ok`, the empty line and ` *`.
   - ` *00` becomes the empty line, returning true.
   - A line framed by `ctl_frame_add` from every text of the earlier story's request table strips back to that text.
4. The host twin frames: piped `R *52`, it prints `ready *` with the checksum of `ready`, then the framed `ok`. Piped `R *53`, it prints the framed `ready`, then the framed `error`.
5. The host twin garbles on request: with `--garble 3`, the third reply after `ready` fails `ctl_frame_strip`, and every other reply checks. With `--garble x`, it prints the usage line and exits with 2.
6. The restart works: `replay.py --command "<host twin> --garble 500" <edge_cases.trace>` passes the trace with 3236 calls and 0 mismatches, and its last line ends with `1 restarts`.
7. The restart limit holds: a test helper program that garbles every reply makes `replay.py` print `failed after 2 restarts` for the trace and exit with 1.
8. Safe printing: when the twin garbles a reply into a character outside ASCII (a test case of the script's own), the restart line holds `\x` escapes and nothing else outside ASCII, and the script does not raise.
9. `make test` still runs the host replay over `edge_cases.trace` and `flaky_transport.trace` with 0 mismatches and 0 restarts, and `make device` still compiles the sketch.
10. `make check` stays green.
11. docs: `docs/control/device-test.md` describes:
    - the frame, with the example `ok *04`;
    - the restart rule, and why a call is never retried;
    - the reset after the port opens, and the speed of 230,400 baud with 4 requests in flight;
    - the new summary line.
    
    Its protocol table shows the lines framed, and its run section says that the owner's D1 mini (FTDI USB, ESP-12F) needs no button press.

## Smoke test

```sh
d=$(mktemp -d); trap 'rm -rf "$d"' EXIT; gcc -std=c99 -ffp-contract=off -IHeishaMon/src/control tests/control/device/host_server.c $(find HeishaMon/src/control -name '*.c') -lm -o "$d/server"; gzip -dc tests/control/data/edge_cases.trace.gz > "$d/edge_cases.trace"; python3 tests/control/device/replay.py --command "$d/server --garble 500" "$d/edge_cases.trace" | tail -1
```

Expect: `device replay: 1 files, 3236 calls, 0 mismatches, 1 restarts`

```sh
d=$(mktemp -d); trap 'rm -rf "$d"' EXIT; gcc -std=c99 -ffp-contract=off -IHeishaMon/src/control tests/control/device/host_server.c $(find HeishaMon/src/control -name '*.c') -lm -o "$d/server"; printf 'R *52\nR *53\n' | "$d/server"
```

Expect: three lines: `ready` with its frame, `ok *04`, and `error` with its frame

## Journal

1. 2026-11-11 human: created.
2. factory: started doing as heishamon.
