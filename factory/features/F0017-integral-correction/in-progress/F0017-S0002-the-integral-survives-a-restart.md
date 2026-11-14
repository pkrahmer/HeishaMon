---
stage: approval
blocked: null
round: 1
attempts: 0
crashes: 0
pull_request: 32
comments_read: []
handed_over: approval
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
- number: 4
  speaker: factory
  kind: started
  at: '2026-11-14T09:01:31.186076Z'
  stage: review
- number: 5
  speaker: reviewer
  kind: result
  at: '2026-11-14T09:03:17.564652Z'
  stage: review
  run: 8cdd3d1f38a1406f8643b3ae9c715067
- number: 6
  speaker: factory
  kind: started
  at: '2026-11-14T09:03:22.506684Z'
  stage: doing
- number: 7
  speaker: builder
  kind: result
  at: '2026-11-14T09:11:58.654690Z'
  stage: doing
  run: 4fc6ec95fde0494b8fbd9609c461a5f9
- number: 8
  speaker: factory
  kind: started
  at: '2026-11-14T09:12:05.415528Z'
  stage: review
- number: 9
  speaker: reviewer
  kind: result
  at: '2026-11-14T09:13:39.040049Z'
  stage: review
  run: 4420bfe0ba1240959a4f309a4d40cb9a
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
4. factory: started review as heishamon.
5. reviewer: Review, round 0. I read the diff and the code it calls (`ctl_payload.c`, `ctl_format.h`, `ctl_controller.c`, `ctl_signals.c`, `ctl_config.c`). The factory's `make check` after the builder was green (journal entry 3).

   1. `tests/control/test_integral_store.c` `test_start_is_held_without_gain`: the test claims to show that with `integral_w_per_kh` 0 the loaded value is held. It cannot show this.
      - No signal is observed, so `ctl_signals_resolve` refuses.
      - The step of 3600 s is above the default `max_step_s` of 300 (`ctl_controller.c:108-112`).
      - So `ctl_advance` skips the interval, `integrate` never runs, and the test passes whatever the gain does.
      - Fix: observe the required signals (indoor, outdoor, produced) with an indoor temperature that differs from the target. Then advance in steps of `max_step_s` or less, so that `integrate` runs with gain 0, and check the integral stays 120.0.
   2. `HeishaMon/src/control/glue/ctl_integral_store.c:22-33`: `is_space` is a second copy of `is_space` in `ctl_payload.c:36`. The trailing-trim loop and the `length == 0` check repeat what `ctl_payload_parse` already does: it calls `skip_spaces` before and after the number, and a number with no digits fails to parse.
      - Fix: remove `is_space`, the loop and the length test, and keep only the refusal of `{` and the `isfinite` check.
   3. `HeishaMon/src/control/glue/ctl_integral_store.h:25-28` and `docs/control/firmware.md` (*The integral file*, "white space after it allowed"): both say only white space after the number is allowed. But `ctl_payload_parse` also skips white space before it, so `" 42"` is accepted.
      - Fix: say "white space around it" in both places, or refuse leading white space.
      - Either way, add a parse row for a leading space.
   4. `HeishaMon/src/control/glue/ctl_integral_store.h:34-36`: the comment says `ctl_format_fixed` refuses NaN and the infinities. It does not: `ctl_format_fixed` writes them as `nan`/`inf` (`ctl_format.h:20`), and the refusal is the store's own `isfinite` check.
      - Fix: correct the comment.
      - Add `NAN` and `INFINITY` rows to `test_format_reads_back`. That refusal is the failing side of the rule, and no test covers it now.
   5. `HeishaMon/src/control/glue/ctl_firmware.h:22-31`: the two edited comments were not rewrapped. Two lines now run far past the width the rest of the file wraps at. Fix: rewrap both comments.
   6. Gap in the story (for the human): three rules of the Description have no criterion and no host test. They live only in `ctl_firmware.cpp`:
      - a failed write is retried at the first check 6 h later, not sooner, and the old saved value is kept;
      - nothing is written at boot for a missing or refused file;
      - `status/integral_saved_w` is published.

      The builder could move the bookkeeping (saved value, time of the last attempt, the outcome of a failed write) into a pure function in `ctl_integral_store.c` and test it. Otherwise the human accepts that these rules go untested.

   Earlier tests: none. The diff adds `tests/control/test_integral_store.c` (status A) and changes, renames or deletes no test file that `develop` holds.

   Checked, nothing to report:
   - **Criteria 1, 2 and 3:** the rows are exact.
   - **Boundaries:** both limits are tested on both sides: 6 h writes and 5 h 59 min does not; a move of 10 W writes and 9.9 W does not.
   - **Upstream files:** none changed (criterion 6).
   - **Includes and names:** they follow the *Architecture* rules.
   - **Buffers:** every buffer is sized at compile time, and the file read is bounded by `sizeof text`.
   - **`replace_file`:** a good reuse of the settings' rename logic.
   - **Decision log:** the entry has the reason and both rejected options that criterion 8 names.
6. factory: started doing as heishamon.
7. builder: Rework, round 1. I fixed findings 1 to 5 of the review. For finding 6 I took the reviewer's first option: what the glue remembers between writes is now pure code, so a host test covers it. `make check` passed (lint, test, device and firmware all green). The smoke test printed `1`, as expected. `git diff develop --stat -- HeishaMon ':!HeishaMon/src'` shows no changes, so no upstream file and no hook line changed.

   **Fixes, by finding**
   1. `tests/control/test_integral_store.c` `test_start_is_held_without_gain`: the test now runs a real hour through `integral_after_an_hour(gain)`.
      - It starts from the stored 120 W.
      - Each minute it observes indoor 23 °C (1 K above the target, inside the trim limit), outdoor 5 °C, and a produced power that keeps the energy away from its bounds.
      - It checks that every `ctl_advance` of 60 s integrates.
      - With gain 0 the integral stays at 120.0. With gain 20 it moves by more than 1 W, which shows that the hour reaches `integrate`.
   2. `HeishaMon/src/control/glue/ctl_integral_store.c`: I removed `is_space`, the trailing-trim loop and the `length == 0` test. What is left is the refusal of `{` and the `isfinite` check.
   3. The header comment and `docs/control/firmware.md` now say "white space around it". There is a new parse row: `" 42"` gives 42.0.
   4. The format comment in the header now says that the store refuses NaN and the infinities itself. `test_format_reads_back` gained rows for `NAN`, `INFINITY` and `-INFINITY`.
   5. `HeishaMon/src/control/glue/ctl_firmware.h`: I rewrapped both comments.
   6. New in `glue/ctl_integral_store.h` and `.c`:
      - `ctl_integral_store` holds two fields: `saved_w`, and `attempt_s` (the time of the last attempt, or the boot).
      - `ctl_integral_store_load`: sets the store at boot and returns the start value. NULL or length 0 means a missing file.
      - `ctl_integral_store_begin`: applies `ctl_integral_store_due`, and when a write is due it records the attempt.
      - `ctl_integral_store_end`: a successful write becomes the saved value; a failed one keeps the old value.
      - `ctl_firmware.cpp` now uses these three in place of its two static variables. `status/integral_saved_w` publishes `integral_store.saved_w`.
      - New tests:
        - `test_load_writes_nothing`: missing, refused and clamped files ask for no write, and the 6 hours count from the boot.
        - `test_failed_write_is_retried_later`: the old saved value is kept, there is no retry before 6 hours, and the retry comes at 6 hours.
        - `test_reset_writes_at_once`.
      - `test_writes_in_a_day` now runs through the store's functions.

   **Documents:** `docs/control/firmware.md` (*The integral file*) names `load`, `begin` and `end` and the store's two fields. In `docs/control/decisions.md` I extended the existing entry by one sentence: the bookkeeping is pure, so the host tests the retry and the load as well. It is still one entry.

   **Rejected:** a pure function that would produce the `status/integral_saved_w` topic and value text. Publishing stays one `ctl_format_fixed` call in the glue. So no host test checks that the topic is published; the human may want to accept that.

   **Noticed, not touched:** as before, the shared rename helper's fallback (remove the target, then rename again) could lose the old integral file if the second rename also fails. `/control.json` already behaves the same way.

   `check`: check: green
8. factory: started review as heishamon.
9. reviewer: Review, round 1: no findings.

   I read the diff against `develop` and the code it calls: `ctl_payload_parse` (`adapt/ctl_payload.c`), `ctl_advance`, `ctl_observe` and `ctl_reset_integral` (`core/ctl_controller.c`), `ctl_loop_init`, the `started` guards in `ctl_firmware.cpp`, and `check.h`. The factory's `make check` after the builder was green (journal entry 7).

   **Findings from round 0, rechecked**
   1. `test_start_is_held_without_gain` now integrates for real.
      - The first `ctl_observe` sets `t_last`, and all three required signals are observed every minute.
      - Steps of 60 s are under `max_step_s`, so `CHECK_TRUE(ctl_advance(...))` holds only when `integrate` ran.
      - The gain-20 control shows that the hour reaches the integral.
   2. The second copy of `is_space` and the trim are gone. `ctl_payload_parse` refuses length 0 by itself: `at_end`, then a bare number with no digits.
   3. The header and `firmware.md` now say "white space around it", and there is a `" 42"` row.
   4. The format comment is correct now, and `test_format_reads_back` has rows for NaN and ±infinity.
   5. `ctl_firmware.h` is rewrapped.
   6. Bookkeeping is now pure code (`ctl_integral_store_load`, `ctl_integral_store_begin`, `ctl_integral_store_end`) and is tested:
      - nothing is written at load, and the 6 hours count from boot, checked on both sides;
      - a failed write keeps the old saved value, is not retried at 11 h 59 min, and is retried at 12 h;
      - a reset writes at once.
      - The only rule still without a test is that `status/integral_saved_w` is published (one `ctl_format_fixed` and `publish` in `publish_telemetry`). The builder named this for the human in entry 7. The human accepts it or rejects it at the approval.

   **Checked, nothing to report**
   - **Criteria 1–4:** the rows are exact, with both boundaries tested. 10 W writes and 9.9 W does not; 6 h writes and 5 h 59 min does not. The simulated day counts writes 1 to 4, each at least 6 h apart.
   - **Architecture:**
     - The new `.c` file includes only `../adapt` and `../loop` headers and the C standard library, and nothing from the core includes it.
     - File access stays in `ctl_firmware.cpp`, and names use `ctl_`/`CTL_`.
     - `git diff --name-status develop...HEAD` lists only files under the fork's folders, plus the work item. No upstream file or hook line changed (criterion 6).
   - **Embedded safety:**
     - The file read is bounded by `sizeof text` (32 bytes), and a larger file is refused before it is read.
     - Every text buffer is written with its size.
     - The parsed value is checked with `isfinite` and clamped to ±`integral_limit_w` before the core gets it.
     - There is no dynamic memory or recursion.
     - The write runs only when `begin` says it is due, so at most every 6 h or on a reset.
   - **Docs:** `firmware.md`, `settings.md` (one sentence) and the one `decisions.md` entry match the code: the names, the 32-byte limit, three decimals in the file and one in the telemetry, and the retry after 6 h. One prose line in `decisions.md` and two in `firmware.md` run past the wrap width. Other lines in these files already do, and it does not change the meaning, so I did not raise it.

   **Earlier tests:** none. The diff adds `tests/control/test_integral_store.c` (status A) and changes, renames or deletes no test file that `develop` holds.
