---
stage: review
blocked: null
round: 0
attempts: 0
crashes: 0
pull_request: 32
comments_read: []
handed_over: null
claim: null
journal:
- number: 1
  speaker: human
  kind: created
  at: '2026-11-14T08:50:14.244365Z'
  stage: doing
- number: 2
  speaker: factory
  kind: started
  at: '2026-11-14T08:50:14.251751Z'
  stage: doing
- number: 3
  speaker: builder
  kind: result
  at: '2026-11-14T09:01:24.151335Z'
  stage: doing
  run: c17842868c684d36b32e84aedc590420
---

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

1. 2026-11-14 human: created.
2. factory: started doing as heishamon.
3. builder: The learned integral now survives a restart. The board keeps it in `/control_integral.txt` and writes the file at most every 6 hours, and only after a move of 10 W. A reset writes 0 at once. The final `make check` passed (lint, test, device and firmware all green), and the smoke test printed `1`, as expected.

   **What I built**
   - `HeishaMon/src/control/glue/ctl_integral_store.h` and `.c` (new, pure C99):
     - `ctl_integral_store_due(integral_w, saved_w, since_write_s, reset)`: the write rule.
     - `ctl_integral_store_parse(text, length, *value_w)`: reads a bare number through `ctl_payload_parse`. It refuses a `{` (so no JSON object), an empty text and any number that is not finite. It allows white space after the number.
     - `ctl_integral_store_start(text, length, limit_w)`: the start value at boot, clamped to ±limit, or 0 for a refused text.
     - `ctl_integral_store_format`: the file's text, with three decimals, through `ctl_format_fixed`.
   - `HeishaMon/src/control/glue/ctl_firmware.cpp`:
     - The temporary-file-and-rename logic of `save_settings` is now a shared helper, `replace_file(path, temporary_path, writer, consequence)`. The settings and the integral both use it. The settings' log lines read as before.
     - `load_integral` runs once in setup, after `ctl_loop_init`. It sets `loop_state.state.p_integral_w` and the saved value, and writes nothing.
     - `store_integral(reset, now)` runs after every decision tick, and with `reset` true in the `integral_reset` handler.
     - `publish_telemetry` also publishes `status/integral_saved_w`, with 1 decimal.
   - `HeishaMon/src/control/glue/ctl_firmware.h`: the comments of setup and loop now mention the integral file.
   - `tests/control/test_integral_store.c`, which checks:
     - the write-rule rows, plus a move of -10 W and a move downwards;
     - the parse rows, plus a trailing line break and a JSON object;
     - the start rows (120, 500, -500, refused, empty);
     - that a loaded value is held with gain 0, through the real `ctl_advance`;
     - that the file text reads back exactly;
     - a simulated day, checked every minute with the integral moving 15 W an hour: at most 4 writes, each at least 6 h after the last;
     - that a steady integral is never written.
     
     I saw it fail at compile, because the header was missing, before writing the code.
   - Documents:
     - `docs/control/firmware.md`: a new section, *The integral file*, and a note about `status/integral_saved_w` under the status table.
     - `docs/control/settings.md`: the sentence about surviving a restart (two short sentences in fact, the second points to `firmware.md`).
     - `docs/control/decisions.md`: one new entry.

   **Decisions where the story left something open**
   - **Telemetry:** `status/integral_saved_w` is published by the glue beside the telemetry's status entries, not as a 13th entry of `ctl_telemetry`. The existing `test_telemetry.c` checks a status count of 12, and no test of another story may change.
   - **Value compared after a load:** the clamped start value, which is the one the core holds.
   - **Failed write:** it counts as a write for the 6 hours, so it is retried at the first check 6 hours later, as the story asks. The old saved value is kept.
   - **File format:** three decimals. A file longer than 32 bytes is refused, so it starts at 0.
   - **The check:** it runs at decision ticks only, not on every loop pass.
   - **Comment:** the one in the reset handler said "nothing is saved"; it now says only the integral's own file is written.

   **Rejected:** extending `ctl_telemetry`'s status group, which would break that test. A core function to set the integral: the glue assigns the caller-owned state field directly, which saved a core change.

   **Criterion checks:** `git diff develop --stat -- HeishaMon ':!HeishaMon/src'` shows nothing, so no upstream file changed and no hook line moved.

   **Noticed, not touched**
   - The shared rename helper keeps the settings' fallback: when a rename fails, it removes the target and renames again. If that second rename also failed, the old integral file would be gone too. This is the same behaviour `/control.json` already has.
   - The control page does not show `integral_saved_w`; the story didn't ask for it.

   `check`: check: green
