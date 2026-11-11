---
stage: approval
blocked: null
round: 2
attempts: 0
crashes: 0
pull_request: 9
comments_read: [6808172827]
handed_over: null
claim: null
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
- number: 3
  speaker: builder
  kind: result
  at: '2026-11-11T21:45:33.041197Z'
  stage: doing
  run: 8c512ec00ca949d9a7747d1aed2d7149
- number: 4
  speaker: factory
  kind: started
  at: '2026-11-11T21:45:39.531791Z'
  stage: review
- number: 5
  speaker: reviewer
  kind: result
  at: '2026-11-11T21:48:07.762844Z'
  stage: review
  run: 5192efe8a9cd40198875f7f92c1eb7b6
- number: 6
  speaker: factory
  kind: started
  at: '2026-11-11T21:48:18.039012Z'
  stage: doing
- number: 7
  speaker: builder
  kind: result
  at: '2026-11-11T21:53:35.897513Z'
  stage: doing
  run: 4ad4ee00b48a4b3391d82c0debce2ee3
- number: 8
  speaker: factory
  kind: started
  at: '2026-11-11T21:53:42.580650Z'
  stage: review
- number: 9
  speaker: reviewer
  kind: result
  at: '2026-11-11T21:55:47.130047Z'
  stage: review
  run: 86db899421c144bfaf1b7b368e0cd373
- number: 10
  speaker: factory
  kind: started
  at: '2026-11-11T21:55:52.846253Z'
  stage: doing
- number: 11
  speaker: builder
  kind: question
  at: '2026-11-11T21:58:45.827962Z'
  stage: doing
  run: 69a748920fb24b2592f610a328372cbb
  blocked: question
- number: 12
  speaker: human
  kind: comment
  at: '2026-11-11T22:06:41.609964Z'
  stage: doing
- number: 13
  speaker: factory
  kind: started
  at: '2026-11-11T22:06:43.998146Z'
  stage: doing
- number: 14
  speaker: builder
  kind: result
  at: '2026-11-11T22:08:29.484366Z'
  stage: doing
  run: 41723177b750435dbe18c3e4a5141279
- number: 15
  speaker: factory
  kind: started
  at: '2026-11-11T22:08:34.739455Z'
  stage: review
- number: 16
  speaker: reviewer
  kind: result
  at: '2026-11-11T22:09:21.808772Z'
  stage: review
  run: 9b30ade049fe4e2495ce042d8a788ed0
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
3. builder: Built the checksum frame, the restart in the replay script, the board reset and the 230,400-baud setting. `make check` is green, and both smoke commands give exactly their `Expect:` lines.

   What I built:
   - `HeishaMon/src/control/wire/ctl_frame.h` and `ctl_frame.c`: `ctl_frame_checksum`, `ctl_frame_add` and `ctl_frame_strip`, as the Interface specifies. They are plain C99 and leave the line unchanged when they refuse.
   - `tests/control/test_frame.c` covers criteria 1–3 as table tests. The round-trip rows are one request of each kind in the protocol table (R, C ×3, B, O, A, M, E, S) plus a text of the full 128 characters. I read "the earlier story's request table" as the protocol table in `device-test.md`.
   - `tests/control/device/host_server.c`:
     - It frames `ready` and every reply, and strips each request's frame first. A request whose frame does not check gets a framed `error` and never reaches `ctl_wire_handle`.
     - `S` is recognised after stripping.
     - Its buffers grow by `CTL_FRAME_SUFFIX`.
     - `--garble <n>` replaces the first character of the n-th reply with `#` and keeps the old checksum. A wrong argument prints the usage line and exits with 2.
   - `tests/control/device/replay/replay.ino`: the same framing, 230,400 baud, and `CTL_FRAME_SUFFIX` added to its line and reply buffers. The stats line is now built with `snprintf` so it can be framed. A refused frame is answered with `error` and never reaches the time sums.
   - `tests/control/device/replay.py`:
     - `--command` is split with `shlex`.
     - The process link is binary and a reader thread feeds a queue, so a reply that never comes times out after 10 s on every platform.
     - With `--port` it sends the reset pulse (DTR off, RTS on, 0.2 s, RTS off) and then waits up to 10 s for a framed `ready`.
     - It keeps 4 requests in flight and restarts a trace from `R` at most twice, dropping replies for 1 s before each restart. Results count only a trace's last attempt.
     - It prints everything from the link safely and ends with the new summary line.
   - Tests for criteria 4–8:
     - `tests/control/device/test_replay.py` builds the twin, checks its frames, `--garble 3` and the usage line, and runs the restart, the restart limit and the safe-printing cases. It prints one line per failed check, in the style of the C tests.
     - `tests/control/device/garbling_twin.py` garbles every reply with a chosen byte, which gives the non-ASCII case.
     - `tests/control/test_replay_script.c` runs the Python test with `system()`, so `make test` covers it without a `Makefile` change.
     - Against the old script and twin, the Python test fails as it should.
   - `docs/control/device-test.md`: new sections for the frame (with `ok *04`) and for restarts (with why a call is never retried), the protocol table shown framed, the reset, 230,400 baud with 4 in flight, the new summary line, and that the owner's D1 mini (FTDI, ESP-12F) needs no button press.
   - `docs/control/decisions.md`: four new entries for the decisions below.

   Decisions where the story left room (each is in `decisions.md`):
   - **Restart line wording.** It is `... restarting (<k> of 2): <what came>`, where `<what came>` is the safe-form broken line, `<request> -> <reply>`, or `no reply within 10 s`. Criterion 8 needs the restart line to carry the `\x` escapes, and the bare form would carry nothing from the link.
   - **Breaks during configuration.** A break while sending R, C or B also restarts the trace, and its place is printed as `<file>` without a line number. A `bad` reply to `B` still fails the trace without a restart, because the same configuration would be bad again.
   - **What counts as broken.**
     - A reply with the right number of fields but a field that does not parse is broken too.
     - A reply whose frame checks but whose text holds a byte outside printable ASCII is broken.
     - Safe printing escapes control characters as well as bytes above `0x7f`.
   - **`--garble` values.** It takes one to nine decimal digits, and `0` garbles nothing. A sign, letters or a longer number is a wrong argument: usage line and exit 2.
   - **Third break.** It prints `<file>:<line>: broken line: <what>` before `<file>: failed after 2 restarts`.
   - **Rejected:** a Python test that `make test` would not run, and a second garble mode inside the host twin.

   Smoke test results:
   - First command: `device replay: 1 files, 3236 calls, 0 mismatches, 1 restarts`.
   - Second command: `ready *6b`, `ok *04`, `error *78`.
   - In `make test`, the host replay ends with `device replay: 2 files, 10639 calls, 0 mismatches, 0 restarts`, and `make device` compiles the sketch (RAM 36 %, flash 23 %).

   Noticed but did not touch:
   - The host twin's `fgets` buffer counts the `\r` of a `\r\n` line, so a framed line at the full length that ends in `\r\n` is answered `error` by the twin, while the sketch takes it. The twin had the same one-byte gap before this story.
   - The reset pulse and 230,400 baud are untested here because no stage has a board. Only the owner's run of `make device-replay` proves them.
   - The `system()` call runs `python3` by name, so `make test` needs `python3` on the PATH. The `Makefile` already needs it there.

   `check`: check: green
4. factory: started review as heishamon.
5. reviewer: Findings:

   1. `tests/control/test_replay_script.c` and `docs/control/decisions.md` (entry "The script's tests are a Python program that a C test runs"): `make test` already runs every `tests/control/device/test_*.py` with `CTL_SOURCES` and `CTL_BUILD` set (CLAUDE.md *Checks*; `Makefile:89-92`). So `test_replay.py` runs twice, and the decision's reason ("a Python test outside `make test`, which nothing would run") is false. Fix: delete `test_replay_script.c`, drop its row and the claim in `device-test.md`, and rewrite or remove the decision entry.
   2. `tests/control/device/test_replay.py:39-46,148`: the test walks `HeishaMon/src/control` itself and builds in its own `tempfile` folder. CLAUDE.md says such a test takes the sources from `CTL_SOURCES` and its scratch folder from `CTL_BUILD`. Fix: use both variables.
   3. `tests/control/device/replay.py:494-503` (`configure`): any reply to `R` or a `C` other than `ok` or `error` fails the trace without a restart. That includes a framed but stale state line left over from the previous attempt, which is a reply with the wrong number of fields and so a restart under the Interface and your own decision ("a break while sending R, C or B also restarts"). Fix: only a `bad …` reply to `B` fails without a restart; every other non-`ok` reply raises `BrokenLine`.
   4. `tests/control/device/test_replay.py`: only the broken-frame trigger of a restart is tested. Nothing tests a restart after an `error` reply, after a reply with the wrong number of fields (or a field that does not parse), or after no reply within 10 s. No criterion names these either, which is a gap against the story. Fix: add these cases, for example as modes of `garbling_twin.py` that go wrong once.
   5. `tests/control/device/test_replay.py:103`: `startswith(trace + ":")` also passes for the configuration form `<file>: broken line…`. The Interface's "with the line of the call" is therefore untested. Fix: expect `<trace>:<n>: broken line, restarting (1 of 2)`, where `<n>` is the trace line of the call whose reply was the 500th.
   6. `tests/control/device/test_replay.py` (criterion 9): no check asserts `0 restarts` for the plain twin. `replay.py` exits 0 whatever the number of restarts, so `make test` stays green even if the host replay restarts. Fix: replay `edge_cases` and `flaky_transport` through the twin with no option, and expect exactly `device replay: 2 files, 10639 calls, 0 mismatches, 0 restarts`.
   7. `tests/control/device/replay.py:347-353,369-383`: the reset pulse (DTR off, RTS on, 0.2 s, RTS off) and `wait_ready` ignoring every other line have no criterion and no test. This is a gap against the story. Fix: test `SerialLink.reset` with a stand-in port object that records the `dtr`/`rts`/sleep sequence, and test `wait_ready` with a twin that prints junk and an unframed `ready` before the framed one.
   8. `tests/control/device/replay/replay.ino:44` (`send_text`) writes the board's reply buffer with `strcpy`, without its size. `host_server.c:84,86` (`strcpy`, `sprintf`) do the same. Fix: write with the buffer's size (`snprintf(reply, sizeof reply - CTL_FRAME_SUFFIX, "%s", text)` and `snprintf` for the stats line).
   9. `replay.ino:37` and `host_server.c:73`: the status of `ctl_frame_add` is discarded with `(void)`. CLAUDE.md *Errors are values* requires the caller to check it. Fix: check it, and on false send a fixed framed `error` (or otherwise handle the refusal).
   10. `tests/control/device/replay/replay.ino:14-15`: the comment narrates an event ("At 921,600 baud the line dropped…; … replayed every trace") instead of a timeless why. Fix: state the lasting reason and cite a `docs/control/decisions.md` entry for the speed and the in-flight count.
   11. The work item's `## Description` no longer matches what was built. It still says:
       - the restart line is `<file>:<line>: broken line, restarting (<k> of 2)`, while the code appends `: <what came>`;
       - "another value than a positive integer garbles nothing", while the code exits with 2 on any non-digit, a sign, or more than nine digits;
       - nothing about configuration breaks printed as `<file>`, the extra `broken line:` line on the third break, or printable-ASCII-only replies counting as broken.
       
       Fix: rewrite the Interface bullets to the built behaviour.
   12. `docs/control/device-test.md` ("Restarts"):
       - It says any argument other than "`--garble` and decimal digits" is refused, but more than nine digits is refused too (`host_server.c:56`).
       - It says a third break prints `<file>: failed after 2 restarts`, but `<place>: broken line: <what>` is printed first (`replay.py:563`).
       
       Fix: state both.

   Earlier tests (changed, not added), each named under "Changed:" in the story's Interface, which is the human's word:
   - `tests/control/device/host_server.c`
   - `tests/control/device/replay.py`
   - `tests/control/device/replay/replay.ino`

   `tests/control/test_wire.c` is unchanged.
6. factory: started doing as heishamon.
7. builder: Round 1: I worked through all twelve review findings. `make check` is green and both smoke commands print exactly their `Expect:` lines.

   What I changed:
   1. **The C test wrapper is gone.** I deleted `tests/control/test_replay_script.c`. `make test` runs `test_replay.py` once, through its own `test_*.py` loop. I removed the wrapper's claim from the parts table in `docs/control/device-test.md`. In `docs/control/decisions.md`, the entry is rewritten as "The script's tests are a Python program beside it", and it now rejects the `system()` wrapper.
   2. **The Python test uses the environment.** `tests/control/device/test_replay.py` builds the twin from `CTL_SOURCES` into a `mkdtemp` folder under `CTL_BUILD`. Without either variable it prints one line and exits with 1. It still takes the traces from `CTL_TRACES`, or unpacks them into that folder when the variable is missing.
   3. **Configuration replies.** In `tests/control/device/replay.py` (`configure`), only a `bad …` reply to `B` now fails the trace without a restart. Every other reply that is not `ok` raises `BrokenLine`, a stale state line included. New decision entry: "Only a bad configuration fails a trace without a restart". `test_configure` covers it with a scripted link. Against the old `replay.py`, its stale-line row and its `bad`-to-`C` row both fail.
   4. **Every kind of restart is tested.** `tests/control/device/garbling_twin.py` is now a proxy around the real host twin. It breaks the n-th reply (`--at`), or every reply, in one of five ways (`--how`): `frame` (with `--byte`), `error`, `fields`, `value` or `silent`.
      - `test_restart_after_answer` breaks reply 500 with `error`, with `fields` and with `value`. Each run expects one restart, 0 mismatches and 3236 calls.
      - `test_restart_after_silence` drops the reply to `R`. It runs `replay.main` inside the test with `REPLY_TIMEOUT_S` set to 0.5 s so it does not wait 10 s, and it expects `<trace>: broken line, restarting (1 of 2): no reply within 0.5 s`.
   5. **The restart line's place is checked.** `test_restart` works out the call whose reply is the 500th (R, the configuration lines and B come first). It then expects the line to start with `<trace>:<line>: broken line, restarting (1 of 2): #`. The `error`, `fields` and `value` cases check the same place with `<request> -> <reply>`.
   6. **Zero restarts is checked.** `test_no_restart` replays `edge_cases` and `flaky_transport` through the plain twin and expects exactly `device replay: 2 files, 10639 calls, 0 mismatches, 0 restarts`.
   7. **Reset and `ready` are tested.**
      - `test_reset` calls `SerialLink.reset` on a stand-in port, with a stand-in for `time`. It expects the sequence dtr False, rts True, sleep 0.2, rts False.
      - `test_wait_ready` uses a scripted link: a junk line with non-ASCII bytes, an unframed `ready`, `ready *00`, then `ready *6b`. It also checks that `wait_ready` gives up when no framed `ready` comes, uppercase digits included.
      - I used stand-ins rather than a junk-printing twin, because they test the same functions without a process.
   8. **Buffer sizes.** `replay.ino` (`send_text`) and `host_server.c` (`answer` and the `ready` line) now write with `snprintf` and the text's share of the buffer. The `strcpy` and `sprintf` calls are gone.
   9. **The frame status is checked.** In both `replay.ino` and `host_server.c`, when `ctl_frame_add` refuses, the reply is sent as the fixed line `error *78`, which the script treats as a restart.
   10. **Timeless comment.** The comment on the baud rate in `replay.ino` now gives the lasting reason and cites the new decision entry "The device replay runs at 230,400 baud with 4 requests in flight".
   11. **Description.** The work item's Interface now matches what was built:
       - the `--garble` rule (one to nine digits, anything else is the usage line and exit 2);
       - non-printable bytes in a reply count as a broken line;
       - the configuration rule from finding 3;
       - the restart line with `: <what came>`, and `<file>` without a line number during the configuration;
       - the extra `broken line:` line printed on a third break;
       - the two new test files listed under New files.
   12. **`device-test.md`, Restarts section.** It now says that more than nine digits is refused, and that a third break prints `<file>:<line>: broken line: <what came>` before `failed after 2 restarts`. It also states the configuration rule and describes the proxy and what each test proves.

   Decisions:
   - The garbling twin wraps the real twin rather than answering `ok` itself. This lets a trace pass after a single break, and the old every-reply cases (the restart limit and the `\xe9` safe printing) still work through it.
   - The silent case drops the reply to `R`, where nothing else is in flight.
   - Rejected: a 10 s real timeout in `make test`, and more `--garble` modes in the C twin.

   Smoke test:
   - First command: `device replay: 1 files, 3236 calls, 0 mismatches, 1 restarts`.
   - Second command: `ready *6b`, `ok *04`, `error *78`.
   - In `make check`: test green (18.4 s), the host replay with 0 restarts, `device` green (RAM 36 %, flash 23 %), and `check: green`.

   Noticed but did not touch:
   - With 4 requests in flight, a reply lost whole (not cut short) would shift the replies after it onto the wrong calls. When the call types match, the field counts match, so that shows up as mismatches, not a restart. A serial line that drops a newline merges two lines, which breaks the frame, so this needs a whole line to vanish. I kept the Interface's rule as it is.
   - The one-byte `\r\n` gap in the twin's `fgets` buffer, the reset pulse and 230,400 baud on a real board, and `python3` needing to be on the PATH are as in the earlier entry: still untouched, and only the owner's run of `make device-replay` proves the board side.

   The factory left out changes outside the write paths: factory/features/F0003-replay-on-real-serial/in-progress/F0003-S0001-framed-lines.md.

   `check`: check: green
8. factory: started review as heishamon.
9. reviewer: Findings:

   1. Work item `## Description` (Interface), and journal entry 7, item 11: the Description in the committed work item is still the original text. The builder's rewrite was outside its write paths, and the factory dropped it (the same entry says so). So the Description still says that a non-positive-integer `--garble` value "garbles nothing", that the restart line is the bare `<file>:<line>: broken line, restarting (<k> of 2)`, and nothing about configuration breaks, printable-ASCII-only replies or the extra `broken line:` line on a third break. The code, `device-test.md` and `decisions.md` all do otherwise, and item 11's claim is false. The builder cannot fix this by editing. Fix: once the code findings below are done, end the next builder run with `question`. List each deviation and ask the human to adopt it into the Description or have it reverted. This follows R13 ("If the description is wrong, ask").
   2. `tests/control/device/test_replay.py` (`test_twin_frames`): the rules "`S` is recognised after stripping" and "each frames every reply, `stats` included" have no test in either direction. Fix: pipe `S *53` and expect a line that `strip_frame` accepts and that starts with `stats calls=`. Pipe a bare `S` and expect the framed `error`.
   3. `tests/control/device/test_replay.py`: nothing tests the failing side of "a request whose frame does not check … reaches neither `ctl_wire_handle` nor the stats". Fix: after `R`/`B`, pipe a call with a wrong checksum, then `S *53`, and expect `stats calls=0`. Also pipe `R *53` after a successful `B` and check that a framed `E` is still answered with a decision line, not `error`.
   4. `tests/control/device/host_server.c:111` (`line[CTL_WIRE_LINE_MAX + CTL_FRAME_SUFFIX + 2]`): with `\r\n`, a framed line of exactly 128 text characters is read as too long and answered `error`. The sketch takes the same line, and the Interface says such a line fits and a trailing `\r` is ignored. No test covers the buffer boundary on either side. Fix: make room for the `\r` (`+ 3`). Add twin tests that a framed 128-character text gets a non-`error` reply with `\n` and with `\r\n`, and that a 129-character framed text gets the framed `error`.
   5. `tests/control/device/test_replay.py` (`test_restart`, `test_restart_limit`): "at most twice" is tested only on the failing side (a third break fails). Nothing tests a trace that breaks twice and then passes with `2 restarts` and exit 0. Fix: let `garbling_twin.py` break more than one chosen reply (for example a repeatable `--at`). Expect `device replay: 1 files, 3236 calls, 0 mismatches, 2 restarts` and exit 0.
   6. `tests/control/device/test_replay.py` (`test_twin_usage`): the nine-digit limit of `--garble` (in `decisions.md` and `device-test.md`) is tested only on the refused side (`1234567890`). Fix: add `--garble 999999999` and expect no usage line, exit 0 and framed replies.

   Notes:
   - `tests/control/device/test_replay.py` (`trace_path`): the fallback that unpacks the traces when `CTL_TRACES` is unset is unreachable under `make test`, which always sets the variable.
   - `tests/control/device/test_replay.py` (`test_restart_after_silence`): silence is tested only on the reply to `R`, so the `<file>:<line>` form for a call that gets no reply within the timeout is not exercised.

   Open, not a finding: the builder's point about a whole reply vanishing with 4 in flight (the later replies then shift onto the wrong calls, which shows up as mismatches instead of a restart) stays as noted in journal entry 7. The human may want it in the Description's question.

   Earlier tests (changed, not added), each named under "Changed:" in the story's Interface, which is the human's word:
   - `tests/control/device/host_server.c`
   - `tests/control/device/replay.py`
   - `tests/control/device/replay/replay.ino`

   `tests/control/test_wire.c` is unchanged.
10. factory: started doing as heishamon.
11. builder: Round 2. I fixed review findings 2 to 6 in code and tests. `make check` is green. Both smoke commands give exactly their `Expect:` lines: `device replay: 1 files, 3236 calls, 0 mismatches, 1 restarts`, and then `ready *6b`, `ok *04`, `error *78`. I am ending with `question` because finding 1 needs the human: the Description and the built behaviour still disagree, and I cannot edit the Description.

    What I changed:
    - `tests/control/device/test_replay.py`:
      - `test_twin_stats` (finding 2): `S *53` gets a framed reply that starts with `stats calls=`, and a bare `S` gets the framed `error`.
      - `test_twin_refuses_broken_frames` (finding 3): after R, the trace's C lines and B, it sends an `E` with a wrong checksum and expects `error`. `S *53` then reports `calls=0`, `R *53` gets `error`, and a correctly framed `E` still gets a 16-field decision line, so the session was not reset.
      - `test_twin_line_length` (finding 4): a framed 128-character text with `\n` or `\r\n`, a 129-character text and a 400-character text. Each gets the framed `error`, and the request after it gets `ok *04`, which shows that the twin stays in step with the lines.
      - `test_two_restarts` (finding 5): breaks reply 500 and reply 1000, expects `device replay: 1 files, 3236 calls, 0 mismatches, 2 restarts` and exit 0.
      - `test_twin_usage` (finding 6): `--garble 999999999` is accepted. There is no usage line, the exit status is 0 and the replies are framed.
    - `tests/control/device/host_server.c`: the line buffer is `+ 3` now, so a framed line of 128 characters fits with `\r\n` (finding 4).
    - `tests/control/device/garbling_twin.py`: `--at` can be given more than once, and the reply count runs over the twin's whole run.
    - `docs/control/device-test.md` (Restarts section): mentions the repeatable `--at` and the two-restart case.

    One limit on finding 4: no valid request is 128 characters long, so a 128-character line is answered `error` whether the twin reads it whole or as too long. The reviewer's suggested test (a framed 128-character text gets a reply other than `error`) cannot be written. The test checks the reply and that the next line still lines up. It does not tell the old buffer from the new one.

    A mutation check (a twin that takes a bare `S`) made `test_twin_stats` fail as it should.

    **Question for the human (finding 1).** The Description differs from what was built in the points below. My rewrite in round 1 was outside my write paths and was dropped. For each point, please adopt the built behaviour into the Description or tell me to revert the code:

    1. **Restart line.** Built: `<file>:<line>: broken line, restarting (<k> of 2): <what came>`. `<what came>` is the broken line in safe form, `<request> -> <reply>`, or `no reply within 10 s`. The Description has the bare form without `: <what came>`. Criterion 8 needs the escapes to appear in that line.
    2. **`--garble` values.** Built: one to nine decimal digits, with `0` garbling nothing. Anything else (a sign, letters, ten or more digits) prints the usage line and exits with 2. The Description says another value than a positive integer "garbles nothing".
    3. **Breaks during configuration.** Built: a break while sending R, C or B also restarts the trace, printed as `<file>: broken line, restarting …` without a line number. Any reply other than `ok` counts as a break, a stale state line included. Only `bad …` to `B` fails the trace without a restart. The Description says nothing about this.
    4. **What counts as broken.** Built: a reply whose frame checks but whose text holds a byte outside printable ASCII is a broken line. So is a reply with the right number of fields where one field does not parse. Safe printing escapes control characters as well as bytes above 0x7f.
    5. **Third break.** Built: `<file>:<line>: broken line: <what came>` is printed before `<file>: failed after 2 restarts`.
    6. **Test files.** Built: `tests/control/device/test_replay.py` and `garbling_twin.py`, which are not in the Interface's list of new files.

    My preference is to adopt all six. Each is recorded in `docs/control/decisions.md` and described in `device-test.md`.

    Also open, for your decision: with 4 requests in flight, a reply that vanishes whole (not cut short) shifts the later replies onto the wrong calls. That shows up as mismatches, not a restart. A dropped newline breaks the frame, so this needs a whole line to vanish. I left the rule as the Interface states it. The alternative is to put a sequence number in each frame, which would change the frame format.

    Noticed but did not touch:
    - The fallback in `trace_path` (unpacking the traces when `CTL_TRACES` is unset) is unreachable under `make test`.
    - Silence is tested only for the reply to `R`.
    - The reset pulse and 230,400 baud are proven only by the owner's `make device-replay` on the board.

    `check`: check: green
12. 2026-11-11 human: Adopt all six as built. This answer and your entry 11 are the Description now for those six points; the reviewer reads them so.

    The vanished whole line: leave the frame format as it is. Record it as a known limit in `device-test.md` with its symptom (mismatches from one line on that do not repeat when the trace is replayed again) and what to do (replay that trace again before believing a mismatch). Nothing else.

    Reviewer: from this round on, a point about the wording of the Description against these six adopted behaviours is not a finding.

    — Kumpel, for the human who owns this project
    <!-- kumpel -->
13. factory: started doing as heishamon.
14. builder: Round 2, after the human's answer in entry 12. The answer adopted the six built behaviours as they are, so they need no change to code or tests. The only thing it asked for was the known limit in the docs, and that is the only change in this round. `make check` is green.

    What I changed:
    - `docs/control/device-test.md`: added a paragraph at the end of "On a mismatch" headed **Known limit: a line that vanishes whole**. It says:
      - With 4 requests in flight, the script matches each reply to the oldest request still waiting. A reply that vanishes whole breaks no frame, so every later reply lands on the call before its own.
      - When the calls are of the same kind, this shows up as mismatches from one line on, not as a restart. The trace usually stops after ten mismatches, before the last request's missing reply would time out.
      - A dropped newline alone merges two lines and breaks the frame, so only a whole line causes this.
      - **Symptom:** mismatches from one line on that do not come back when the trace is replayed again.
      - **What to do:** replay that trace again before believing a mismatch. A mismatch from the core comes back at the same line every time.
      - The frame format stays as it is, with no sequence number.

    No changes to code, tests or `decisions.md`. Following the answer's "Nothing else", I did not add a decision entry for the limit.

    Smoke test: I did not run it again this round, because no code changed since round 2's run (entry 11), which printed exactly the `Expect:` lines. In this run, `make check` ended with `device replay: 2 files, 10639 calls, 0 mismatches, 0 restarts`, `test: green`, `device: green` (RAM 36 %, flash 23 %) and `check: green`.

    Noticed but did not touch, as in entry 11:
    - The fallback in `trace_path` of `test_replay.py` is unreachable under `make test`.
    - A timeout is tested only for the reply to `R`.
    - The reset pulse and 230,400 baud are proven only by the owner's `make device-replay` on the board.

    `check`: check: green

    `check`: check: green
15. factory: started review as heishamon.
16. reviewer: no findings

    I read the whole diff against the Description and against the six built behaviours the human adopted in entry 12. I also checked that every finding from rounds 1 and 2 is fixed in the code.

    - **Criteria 1–3:** `tests/control/test_frame.c` holds the table rows the story asks for, plus `ok *0A` and a 128-character round trip.
    - **Criteria 4–9:** `tests/control/device/test_replay.py` covers them.
      - Twin: frames, `S` (framed and bare), refused frames leaving the stats at `calls=0`, line length 128 and 129 with `\n` and `\r\n`, `--garble 3`, and the usage line on both sides of nine digits.
      - Restarts: after a broken frame, `error`, the wrong fields, a field that does not parse, and silence. Also two restarts with a pass, the limit, safe printing with `\xe9`, and 0 restarts on the plain twin.
      - Script functions: reset, `wait_ready` and `configure`.
    - **Architecture:** `ctl_frame.c` and `ctl_frame.h` are strict C99. Their names use `ctl_`/`CTL_`, they use no dynamic memory, every loop has a bound, and refusals leave the line unchanged.
    - **Buffers:**
      - The sketch's line buffer (`CTL_WIRE_LINE_MAX + CTL_FRAME_SUFFIX + 2`) holds a framed 128-character text with `\r`. The twin's is `+ 3` for `fgets`.
      - Every reply is written with `snprintf` and its text's share of the buffer.
      - When `ctl_frame_add` refuses, both sides send a fixed `error *78`.
    - **Docs:** I checked the frame values in `docs/control/device-test.md` by hand: `R *52`, `B *42`, `S *53`, `ready *6b` and `error *78` are right. The new known-limit paragraph matches `replay_calls`, which pairs each reply with the oldest pending request (`popleft`), and the drain after ten mismatches.
    - **Description:** It matches the build, read together with entry 12, which adopted the six behaviours.

    Notes (not findings):
    - `tests/control/device/test_replay.py` (`trace_path`): the fallback that unpacks traces when `CTL_TRACES` is unset never runs under `make test`.
    - `tests/control/device/test_replay.py` (`test_restart_after_silence`): silence is tested only on the reply to `R`, so `<file>:<line>` after a call that times out is not exercised.
    - `tests/control/device/test_replay.py` (`test_twin_line_length`): no valid request is 128 characters long, so this test cannot tell the twin's `+ 3` buffer from the old `+ 2`. It does prove the twin stays in step with the lines.

    Earlier tests (changed, not added). Each is named under "Changed:" in the story's Interface, which is the human's word:
    - `tests/control/device/host_server.c`
    - `tests/control/device/replay.py`
    - `tests/control/device/replay/replay.ino`

    `tests/control/test_wire.c` is unchanged.
