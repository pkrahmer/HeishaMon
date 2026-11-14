# The integral survives a restart

## Description

**Why.** The integral of S0001 learns slowly, over about a day. A restart (a flash, a power cut, a crash) would throw that away and leave the house with its offset again until it has learned anew. The owner also cares about flash wear, so the integral is saved rarely.

**What it does.** The board keeps the learned integral in its own file `/control_integral.txt`, one number in W as text. It writes the file only when both are true: at least 6 hours have passed since the last write (or since boot, for the first write), and the integral differs by 10 W or more from the value last written (or loaded). A reset over `integral_reset` writes 0 at once, regardless of the 6 hours. Nothing else writes the file, and the file is never part of `/control.json`.

**At boot.** The board reads the file once, after the settings. A value that is a finite number is clamped to ±`integral_limit_w` and becomes the integral's start value. A missing file, an empty file or a value that is not a finite number starts at 0, and nothing is written for it. With `integral_w_per_kh` 0 the loaded value is held as it is (S0001: 0 freezes, it does not clear).

**Writing.** The file is written as `/control.json` is: to a temporary file first, then renamed, so that a power cut during a write leaves the old value or the new one, never a broken one. A failed write keeps the old file and is retried at the next check, not sooner than 6 hours later.

**Telemetry.** `<base>/control/status/integral_saved_w`: the value last written or loaded, published with the other status values.

## Interface

- The rule when to write is a pure function in the glue's `.c` code, so that a host test can check it: it takes the integral, the value last saved, the seconds since the last write and whether a reset happened, and returns whether to write.
- The parsing of the file's text is a pure function too: it returns the value or a refusal.
- The file access lives in `HeishaMon/src/control/glue/ctl_firmware.cpp`.
- Tests: new host test `tests/control/test_integral_store.c`. No test another story wrote changes.
- No upstream file changes, and no hook line changes.

## Acceptance criteria

1. The write rule, rows of one test: 6 h passed and 10 W moved writes; 5 h 59 min and 50 W moved does not; 7 h and 9.9 W moved does not; a reset writes at 0 h and 0 W moved.
2. Parsing, rows of one test: `"42.5"` gives 42.5; `"-12"` gives -12.0; `""`, `"abc"`, `"nan"`, `"inf"` and `"1e999"` are refused.
3. At boot, rows: a stored 120 with `integral_limit_w` 300 starts at 120.0; a stored 500 starts at 300.0; a stored -500 at -300.0; a refused file starts at 0.0. A host test checks this through the glue's pure functions.
4. With the integral constant, the file is written at most once in any 6 hours of a simulated day; a host test counts the writes over 24 simulated hours with the integral moving by 15 W each hour: at most 4.
5. `make firmware` builds both boards.
6. `git diff develop --stat -- HeishaMon ':!HeishaMon/src'` shows no upstream file changed.
7. `make check` stays green.
8. docs: `docs/control/firmware.md` describes the file, when it is written and how it is read at boot; `docs/control/settings.md` says in one sentence that the learned integral survives a restart; `docs/control/decisions.md` gets one entry: its own file, at most every 6 hours and only after a move of 10 W, for flash wear; rejected: a retained MQTT copy (the broker may be down at boot) and `/control.json` (a settings file should change only when a setting does).

## Smoke test

```sh
make test 2>&1 | grep -E '^control: [0-9]+ test programs passed$' | wc -l
```

Expect: `1`

## Journal
