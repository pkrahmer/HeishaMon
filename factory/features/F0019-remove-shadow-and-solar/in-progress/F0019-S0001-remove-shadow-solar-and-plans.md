---
stage: approval
blocked: null
round: 2
attempts: 0
crashes: 0
pull_request: 34
comments_read: [6875920189]
handed_over: null
claim: null
journal:
- number: 1
  speaker: human
  kind: created
  at: '2026-11-14T10:14:20.998119Z'
  stage: doing
- number: 2
  speaker: factory
  kind: started
  at: '2026-11-14T10:14:21.004586Z'
  stage: doing
- number: 3
  speaker: builder
  kind: question
  at: '2026-11-14T10:28:44.150499Z'
  stage: doing
  run: 0e400c7fd54a4188bf1c942baa8a62f0
  blocked: question
- number: 4
  speaker: human
  kind: comment
  at: '2026-11-14T10:29:31.738994Z'
  stage: doing
- number: 5
  speaker: factory
  kind: started
  at: '2026-11-14T10:29:34.039080Z'
  stage: doing
- number: 6
  speaker: builder
  kind: result
  at: '2026-11-14T10:35:05.019628Z'
  stage: doing
  run: 45553d0457ea48428e1e4de11c7cd0ad
- number: 7
  speaker: factory
  kind: started
  at: '2026-11-14T10:35:10.173864Z'
  stage: review
- number: 8
  speaker: reviewer
  kind: result
  at: '2026-11-14T10:36:34.238124Z'
  stage: review
  run: 5a377b0aa9f543a7ac8939a8ddfd48ab
- number: 9
  speaker: factory
  kind: started
  at: '2026-11-14T10:36:39.996760Z'
  stage: doing
- number: 10
  speaker: builder
  kind: result
  at: '2026-11-14T10:41:43.107479Z'
  stage: doing
  run: e64ca721483446a78d37036daa706621
- number: 11
  speaker: factory
  kind: started
  at: '2026-11-14T10:41:50.968255Z'
  stage: review
- number: 12
  speaker: reviewer
  kind: result
  at: '2026-11-14T10:42:43.589747Z'
  stage: review
  run: 572ad74bc1af4b7896b365b46c811b0e
- number: 13
  speaker: factory
  kind: started
  at: '2026-11-14T10:42:48.680300Z'
  stage: doing
- number: 14
  speaker: builder
  kind: result
  at: '2026-11-14T10:47:46.129769Z'
  stage: doing
  run: 6d0811e57c5a496785bff52c2a242625
- number: 15
  speaker: factory
  kind: started
  at: '2026-11-14T10:47:50.999974Z'
  stage: review
- number: 16
  speaker: reviewer
  kind: result
  at: '2026-11-14T10:48:18.284969Z'
  stage: review
  run: c08cc4b190074086b7513072c3babd82
---

# Remove the shadow, the solar input and the planning documents

## Description

**Why.** The owner wants the fork to carry only what the production board uses. Three things are left that nothing uses: the shadow (a live comparison of the C controller against Node-RED, which is off), the solar input (the owner's house gains too little from the sun to model; the proportional and integral corrections absorb it), and two planning documents that later documents replaced.

**The shadow goes.** `HeishaMon/src/control/loop/ctl_shadow.c` and `.h`, the shadow tool under `tests/control/device/` (`shadow/shadow.ino`, `shadow.py`, `shadow_host.c` and anything only they use), `tests/control/test_shadow.c`, and `docs/control/shadow.md`. If the `Makefile` names any of them, the builder asks before going on.

**The solar input leaves the board.** The settings `pv_topic` and `solar_gain_factor`, the subscription and handling of the PV topic, the telemetry value `inputs/solar_w`, and their rows on the control page and in `docs/control/settings.md`. The glue sets the core's `solar_gain_factor` to 0 and never feeds the core's solar signal, so the signal falls back to 0 W as it does today with `pv_topic` empty. The core itself does not change: its solar signal and field stay, so that every golden trace replays bit for bit.

**Two documents go.** `docs/control/algorithm-plan.md` and `docs/control/firmware-design.md`. Every link to them from another document is removed or pointed to `core.md`, `firmware.md` or `decisions.md`.

**What stays.** Everything else, and the production board's behaviour: with `pv_topic` empty today, the board computes after this story exactly what it computes now.

## Interface

- Removed: the shadow module and tool, the two settings, the PV path in the glue, `inputs/solar_w`.
- The core (`HeishaMon/src/control/core/`) does not change.
- The leave for tests another story wrote: `tests/control/test_shadow.c` and any device test that tests only the shadow may be deleted. In `tests/control/test_settings*.c`, `tests/control/test_telemetry*.c`, `tests/control/test_form*.c`, `tests/control/test_lean_page.c`, `tests/control/test_html.c`, `tests/control/test_loop.c` and `tests/control/test_dispatch.c`, rows and checks for the removed settings and value may be deleted and indexes and counts may shift; no check of kept behaviour may be removed or loosened.
- No upstream file changes, and no hook line changes.

## Acceptance criteria

1. `git grep -n -E "ctl_shadow|pv_topic|solar_w\b|algorithm-plan|firmware-design" -- HeishaMon tests docs/control ':!docs/control/decisions.md'` prints nothing, and `git grep -n solar_gain_factor -- HeishaMon/src/control/glue docs/control/settings.md` prints nothing.
2. `git diff develop --stat -- HeishaMon/src/control/core` prints nothing.
3. A host test loads a `/control.json` holding `pv_topic` and `solar_gain_factor` with values: it loads, both names are ignored, every other setting in it is applied, and the loop's configuration has `solar_gain_factor` 0.
4. Every golden trace replays bit for bit, as before.
5. `make firmware` builds both boards; the D1 mini's global RAM and flash are no larger than on `develop`. Report the before and after numbers.
6. `git diff develop --stat -- HeishaMon ':!HeishaMon/src'` shows no upstream file changed.
7. `make check` stays green.
8. docs: `docs/control/settings.md`, `docs/control/firmware.md`, `docs/control/loop.md` and `docs/control/device-test.md` describe only what remains; `docs/control/decisions.md` gets one entry: the shadow, the solar input and the two planning documents removed, because nothing on the production board uses them; rejected: removing the solar signal from the core (the golden traces are compared bit for bit and nobody edits them).

## Smoke test

```sh
make test 2>&1 | grep -E '^control: [0-9]+ test programs passed$' | wc -l
```

Expect: `1`

## Journal

1. 2026-11-14 human: created.
2. factory: started doing as heishamon.
3. builder: Question: criterion 1, as written, cannot pass without breaking criterion 2 and the Description. Everything else is built, and `make check` is green.

   1. `solar_w\b` also matches the core's field `p_solar_w`, which the story keeps. It appears in `core/ctl_balance.c:40,78`, `core/ctl_signals.c:100` and `core/ctl_signals.h:30`. It is also in code and documents that only use that field: `loop/ctl_loop.c:248`, `adapt/ctl_pump.c:126-139` (a local variable named `solar_w`), `docs/control/core.md`, `docs/control/reference.md`, the Python reference, `tests/control/test_balance.c`, `test_signals.c` and `test_loop.c:475,519`.
   2. The second grep finds `glue/ctl_settings.c:586`, `config->core.solar_gain_factor = 0.0;`. The Description asks for exactly this line ("The glue sets the core's solar_gain_factor to 0").

   Options:
   (a) Read criterion 1 as aimed at the telemetry name and the setting. The first grep would use `(^|[^_])solar_w\b`, which prints nothing now. The second grep would accept the one line that sets the core's field to 0.
   (b) Hide the names: rename the adapter's local variable, and move the zeroing into `loop/` (for example a new `ctl_loop_config_defaults` value). This changes loop defaults and `test_loop.c` rows that test kept behaviour, and it still cannot clear `p_solar_w` from the core.
   (c) Reword the criterion some other way.
   I would choose (a), and nothing further would change.

   Built:
   - Deleted the shadow: `HeishaMon/src/control/loop/ctl_shadow.c/.h`, `tests/control/device/shadow/shadow.ino`, `shadow.py`, `shadow_host.c`, `test_shadow.py`, `shadow.example.json`, `data/shadow_capture.jsonl`, `tests/control/test_shadow.c` and `docs/control/shadow.md`. The `Makefile` names none of them, so I did not need to ask about it.
   - Deleted `docs/control/algorithm-plan.md` and `docs/control/firmware-design.md`. Their links in `adapters.md`, `golden-traces.md`, `router.md`, `reference.md`, `loop.md`, `firmware.md` and the docstrings of `docs/control/python-reference/*.py` (comments only) were removed or now point to `core.md`, `firmware.md` or `decisions.md`.
   - The glue changes:
     - `glue/ctl_settings.c/.h`: `pv_topic` and `solar_gain_factor` are gone, `CTL_SETTINGS_FIXED_NAMES` is now 17, and `ctl_settings_to_loop` sets the core's `solar_gain_factor` to 0.
     - `glue/ctl_dispatch.c/.h`: no PV topic match.
     - `glue/ctl_firmware.cpp/.h`: no PV subscription. `moves_topics` drops its `_topic` suffix case, because no setting uses it any more.
     - `glue/ctl_telemetry.c`: `inputs/solar_w` is gone, so there are 8 fixed input entries.
   - Shadow wording changed in the comments of `loop/ctl_loop.c` and `loop/ctl_loop.h`. `assume_landed` stays, because the gate uses it.
   - New test `tests/control/test_settings_solar.c` (criterion 3). It loads the pairs of a `/control.json` holding both old names with values, among kept settings. Both old names get `UNKNOWN`, every other setting is applied, the set is valid, the names are no longer listed, and the loop's configuration has `solar_gain_factor` 0 (also from the defaults). The old PV topic now dispatches as NONE. It failed before the code change and passes now. The old topic's name is split into two string literals, as `test_settings_remote.c` already does.
   - Changed tests, using the story's permission:
     - `test_settings.c`: removed the rows for the removed settings and shifted the name counts and indexes by 2. The decimals and formatting rows for `solar_gain_factor` now use `internal_w`. The topic checks for `pv_topic` now use `room.0.topic`. `test_to_loop` now expects a gain of 0.
     - `test_settings_remote.c`: its kept-setting pair `pv_topic` is replaced by `desired_indoor_c`.
     - `test_form.c` and `test_form_rows.c`: the piece, length and unknown-name checks now use `room.0.topic`; one report index shifts.
     - `test_html.c`: the refused row's name is now `room.0.topic`.
     - `test_dispatch.c`: the PV rows are removed.
     - `test_telemetry.c`: the `inputs/solar_w` row and fixture line are removed, and counts and indexes are shifted by one.
     - No check of kept behaviour was removed or loosened.
   - Docs:
     - `settings.md`: both rows and the `pv_topic` mention are removed.
     - `firmware.md`: rows, subscription, telemetry and first-start text updated, plus a new paragraph "No solar input".
     - `loop.md`: no shadow caller; says the firmware never offers `CTL_INPUT_PV`.
     - `device-test.md` already described only what remains, so it is unchanged.
     - `decisions.md`: a new entry, including the rejected option of removing the solar signal from the core.

   Decision (recorded in `decisions.md`): the loop keeps `CTL_INPUT_PV`, `ctl_solar` and `ctl_loop_format_record`. The Interface does not list them, and their `test_loop.c` checks test kept loop behaviour. After this story nothing in the firmware calls them. Removing them would be a follow-up story.

   Results:
   - Criterion 2: `git diff develop --stat -- HeishaMon/src/control/core` prints nothing.
   - Criterion 4: the golden traces and the device replay pass, with 0 mismatches.
   - Criterion 5, memory before and after:
     - D1 mini RAM: 48412 B before, 48244 B after.
     - D1 mini flash: 757528 B before, 757144 B after.
     - ESP32-S3 sketch: 1705491 B before, 1705151 B after.
     - ESP32-S3 globals: 67488 B before, 67368 B after.
   - Criterion 6: `git diff develop --stat -- HeishaMon ':!HeishaMon/src'` prints nothing.
   - Criterion 7: `make check` is green ("control: 35 test programs passed").
   - Smoke test: prints `1`, as expected.
4. 2026-11-14 human: (a). My criterion was aimed at the telemetry name and the settings, not at the core's `p_solar_w` or locals that use it. Read criterion 1 as: `(^|[^_])solar_w\b` finds nothing outside the core and the local in `adapt/ctl_pump.c`, and the second grep accepts the one line in `glue/ctl_settings.c` that sets the core's field to 0, which the Description asks for.

   Your decision on `CTL_INPUT_PV`, `ctl_solar` and `ctl_loop_format_record` stands; leave them for now.
5. factory: started doing as heishamon.
6. builder: I followed the human's answer (a). The code, tests and documents already built in the last round meet criterion 1 as the human now reads it, so I changed no code.

   What I did this round:
   - In the work item's `## Description`, I rewrote criterion 1 to say what the answer says. The first grep uses `(^|[^_])solar_w\b` and finds nothing outside the core and the local variable `solar_w` in `adapt/ctl_pump.c`. The second grep finds only the line in `glue/ctl_settings.c` that sets the core's `solar_gain_factor` to 0.
   - I checked the state of the branch:
     - The first grep finds only `adapt/ctl_pump.c:126,135,136,139`.
     - The second grep finds only `glue/ctl_settings.c:586`.
     - `git diff develop --stat` prints nothing, both for the core and for upstream files outside `HeishaMon/src`.
   - As the human said, `CTL_INPUT_PV`, `ctl_solar` and `ctl_loop_format_record` stay in the loop.

   Results:
   - `make check` is green: the device replay has 0 mismatches and "control: 35 test programs passed".
   - D1 mini memory, the same numbers as last round: RAM 48412 → 48244 B, flash 757528 → 757144 B.
   - ESP32-S3: sketch 1705491 → 1705151 B, globals 67488 → 67368 B.
   - Smoke test: prints `1`, as expected.

   Everything I built is listed in journal entry 3, and nothing in it changed. Still open, not touched: in the firmware nothing uses the loop's PV input, `ctl_solar` or `ctl_loop_format_record` any more. Removing them would be a follow-up story for the human to decide.

   The factory left out changes outside the write paths: factory/features/F0019-remove-shadow-and-solar/in-progress/F0019-S0001-remove-shadow-solar-and-plans.md.

   `check`: check: green
7. factory: started review as heishamon.
8. reviewer: Review, round 0. The code, the tests and criteria 1 to 7 hold. Criterion 1 holds as the human now reads it (entry 4): the first grep finds only the local `solar_w` in `adapt/ctl_pump.c`, and the second finds only `glue/ctl_settings.c:586`. The core and the upstream files show no diff. The factory's `make check` after the builder was green. The findings below are about the docs (criterion 8) and one header comment.

   Findings:
   1. `docs/control/firmware.md:114` says "The five input values are `-` when the decision has no inputs". With `inputs/solar_w` gone, four value entries are left: `indoor_c`, `outdoor_c`, `produced_w` and `internal_w` (compare `tests/control/test_telemetry.c` `test_without_inputs`, which lists four). Fix: say "four".
   2. `docs/control/firmware.md:98` says "8 entries, 3 for each configured room and the 2 frame counters". This line was edited by this diff, but `glue/ctl_telemetry.c` publishes no frame counter. Its 8 fixed entries are the table's first eight rows, `indoor_c` to `rooms_fresh_weight`. Criterion 8 asks `firmware.md` to describe only what remains. Fix: something like "8 entries, plus 3 for each configured room".
   3. `HeishaMon/src/control/glue/ctl_settings.h:82-87`: the comment on `ctl_settings_to_loop` says it writes the loop's defaults with the core's tuning from the settings. It does not say that the function now forces the core's `solar_gain_factor` to 0 instead of the loop's default of 0.02, which a caller can see in the result (`test_settings.c` `test_to_loop` now has to patch `expected.core.solar_gain_factor`). Fix: add "and a solar_gain_factor of 0" to that comment, as `firmware.md:71` already says.

   Earlier tests (group 6), each changed or deleted under the leave the story's Interface names with the human's word:
   - `tests/control/test_dispatch.c` (M): rows for the removed PV topic deleted.
   - `tests/control/test_form.c` (M): removed setting's name replaced by `room.0.topic`; one report index shifts.
   - `tests/control/test_form_rows.c` (M): removed setting's name replaced by `room.0.topic`.
   - `tests/control/test_html.c` (M): removed setting's name replaced by `room.0.topic`.
   - `tests/control/test_settings.c` (M): rows for the removed settings deleted, counts and indexes shifted; kept checks moved to `internal_w` or `room.0.topic`, none loosened.
   - `tests/control/test_settings_remote.c` (M): a kept-setting pair switched from `pv_topic` to `desired_indoor_c`. The Interface's `test_settings*.c` covers it.
   - `tests/control/test_telemetry.c` (M): `inputs/solar_w` row and fixture line deleted, counts shifted by one.
   - `tests/control/test_shadow.c` (D): shadow test, may be deleted.
   - `tests/control/device/test_shadow.py` (D): a device test of only the shadow, may be deleted.
   - `tests/control/device/shadow_host.c`, `shadow.py`, `shadow/shadow.ino`, `shadow.example.json` and `data/shadow_capture.jsonl` (D): the shadow tool and its data, named by the Description.

   Checked and found right:
   - Criterion 3's test, `tests/control/test_settings_solar.c`, uses the same pair pattern as `test_settings_remote.c`. The real loader, `load_settings` in `ctl_firmware.cpp`, needs LittleFS and ArduinoJson and cannot run on the host. The test does show both old names getting `UNKNOWN`, the other settings applied, the set valid, and `solar_gain_factor` 0 both from the file and from the defaults.
   - The production board computes the same as before: the core's `fallback_w[P_SOLAR]` is 0, and nothing on the board fed `P_SOLAR` before or after.
   - Dropping `_topic` from `moves_topics` is safe: no fixed name ends in `_topic` any more.
   - The diff adds no dynamic memory, recursion or unbounded loop.
   - `decisions.md` has the required entry, with the rejected option.
   - `settings.md`, `loop.md` and `device-test.md` match the code.
9. factory: started doing as heishamon.
10. builder: Round 1: I fixed exactly the three findings from the reviewer's entry 8. Nothing else changed.

    1. `docs/control/firmware.md`: "The five input values are `-`" now says "four", which matches `indoor_c`, `outdoor_c`, `produced_w` and `internal_w`.
    2. `docs/control/firmware.md`: the inputs heading line now reads "8 entries, plus 3 for each configured room". The wrong "2 frame counters" is gone, because `glue/ctl_telemetry.c` publishes no frame counter.
    3. `HeishaMon/src/control/glue/ctl_settings.h`: the comment on `ctl_settings_to_loop` now says the function writes a `solar_gain_factor` of 0, because the board has no solar input.

    I changed no code and no tests. Comments and documents only, so the golden traces are unaffected.

    Results:
    - `make check` is green.
    - D1 mini memory is unchanged from round 0:
      - RAM: 48412 B on `develop`, 48244 B now.
      - Flash: 757528 B on `develop`, 757144 B now.
    - ESP32-S3 sketch: 1705491 B before, 1705151 B now.
    - ESP32-S3 globals: 67488 B before, 67368 B now.
    - Smoke test: prints `1`, as expected.

    Still open, as the human decided in entry 4: `CTL_INPUT_PV`, `ctl_solar` and `ctl_loop_format_record` stay in the loop. Nothing in the firmware uses them any more. Removing them would be a follow-up story.

    `check`: check: green
11. factory: started review as heishamon.
12. reviewer: Review, round 1. All three findings from round 0 are fixed. In round 1 the builder changed only `docs/control/firmware.md` and the comment in `glue/ctl_settings.h`. One new problem came with the fix for my own finding 3, and it fails criterion 1.

    Findings:
    1. `HeishaMon/src/control/glue/ctl_settings.h:85`: the comment I asked for in round 0 (finding 3) contains the literal name `solar_gain_factor`. Criterion 1's second grep, `git grep -n solar_gain_factor -- HeishaMon/src/control/glue docs/control/settings.md`, now finds two lines: this comment and `glue/ctl_settings.c:586`. The human's reading in entry 4 allows only the one line in `glue/ctl_settings.c` that sets the core's field to 0. Fix: say the same thing without the name, for example "and a solar gain of 0, since the board has no solar input", so the grep again finds only `ctl_settings.c:586`. I should have asked for wording that keeps the grep clean.

    Earlier tests (group 6). These are unchanged since round 0, and each was changed or deleted under the leave that the story's Interface gives with the human's word:
    - `tests/control/test_dispatch.c` (M): PV rows deleted.
    - `tests/control/test_form.c` (M): the removed name is replaced by `room.0.topic`, and one index shifts.
    - `tests/control/test_form_rows.c` (M): the removed name is replaced by `room.0.topic`.
    - `tests/control/test_html.c` (M): the removed name is replaced by `room.0.topic`.
    - `tests/control/test_settings.c` (M): rows for the removed settings deleted and counts and indexes shifted. Kept checks moved to `internal_w` or `room.0.topic`, and none was loosened.
    - `tests/control/test_settings_remote.c` (M): the kept-setting pair `pv_topic` is now `desired_indoor_c`.
    - `tests/control/test_telemetry.c` (M): the `inputs/solar_w` row and its fixture line deleted, and counts shifted.
    - `tests/control/test_shadow.c` and `tests/control/device/test_shadow.py` (D): shadow-only tests, which the story allows deleting.
    - `tests/control/device/shadow_host.c`, `shadow.py`, `shadow/shadow.ino`, `shadow.example.json` and `data/shadow_capture.jsonl` (D): the shadow tool and its data, which the Description names.

    Checked and found right:
    - `firmware.md` now says "8 entries, plus 3 for each configured room" and "four input values". Both match `put_fixed_input` in `glue/ctl_telemetry.c` (cases 0 to 7 with no `solar_w`).
    - `firmware.md` says the loader skips the old names and the next save drops them. This matches `load_settings` (it logs and skips a non-`OK` status) and `write_settings` (it writes only the listed names).
    - The first grep, with the human's pattern `(^|[^_])solar_w\b`, finds only the local `solar_w` in `adapt/ctl_pump.c`. `ctl_shadow`, `pv_topic`, `algorithm-plan` and `firmware-design` appear nowhere in scope. Any other "shadow" text is CSS in upstream's `htmlcode.h`.
    - The factory's `make check` after the builder was green (entry 10).
    - Information only, not a finding: the builder's rewrite of criterion 1 in round 0b was left out by the factory, because the work item is outside the builder's write paths. The `## Description` therefore still shows the original criterion 1. The human's reading of it is in journal entry 4.
13. factory: started doing as heishamon.
14. builder: Round 2: I fixed the one finding from the reviewer's entry 12. Nothing else changed.

    1. `HeishaMon/src/control/glue/ctl_settings.h`: the comment on `ctl_settings_to_loop` no longer uses the literal name `solar_gain_factor`. It now says "and a solar gain of 0, since the board has no solar input", and I reflowed the comment's lines. Comment only: no code, test or document changed.

    Criterion 1, using the human's reading from entry 4:
    - `git grep -n solar_gain_factor -- HeishaMon/src/control/glue docs/control/settings.md` now finds only `glue/ctl_settings.c:586`, the line that sets the core's field to 0.
    - `(^|[^_])solar_w\b|ctl_shadow|pv_topic|algorithm-plan|firmware-design` finds nothing in scope outside the core except the local `solar_w` in `adapt/ctl_pump.c:126,135,136,139`.

    Results:
    - `make check` is green, and the device replay has 0 mismatches.
    - Memory is the same as last round:
      - D1 mini RAM: 48412 B on `develop`, 48244 B now.
      - D1 mini flash: 757528 B on `develop`, 757144 B now.
      - ESP32-S3 sketch: 1705491 B before, 1705151 B now.
      - ESP32-S3 globals: 67488 B before, 67368 B now.
    - Smoke test: prints `1`, as expected.

    Still open, as the human decided in entry 4: `CTL_INPUT_PV`, `ctl_solar` and `ctl_loop_format_record` stay in the loop, though nothing in the firmware uses them any more. Removing them would be a follow-up story. The work item's `## Description` still shows the original criterion 1. The human's reading of it is in entry 4.

    `check`: check: green
15. factory: started review as heishamon.
16. reviewer: Review, round 2: no findings.

    The round-2 change between `b5df311` and `e1983bf` touches one file, the comment on `ctl_settings_to_loop` in `HeishaMon/src/control/glue/ctl_settings.h`. It now says "a solar gain of 0, since the board has no solar input" and no longer uses the literal setting name. Nothing else changed since round 1.

    Groups 1 to 5:
    - Criterion 1, read as the human reads it in entry 4:
      - The pattern `(^|[^_])solar_w\b|ctl_shadow|pv_topic|algorithm-plan|firmware-design` finds only the local `solar_w` in `adapt/ctl_pump.c:126,135,136,139`. The search covered `HeishaMon`, `tests` and `docs/control`, leaving out `decisions.md` and the core.
      - `git grep -n solar_gain_factor -- HeishaMon/src/control/glue docs/control/settings.md` finds only `glue/ctl_settings.c:586`, the line that sets the core's field to 0.
    - Criteria 2 and 6: `git diff develop --stat` prints nothing, both for the core and for `HeishaMon` outside `HeishaMon/src`.
    - Criterion 3: `tests/control/test_settings_solar.c` stands as reviewed in rounds 0 and 1.
    - Criteria 4, 5 and 7: the factory's `make check` after the builder was green (entry 14). The memory numbers are reported, and the D1 mini's RAM and flash both went down.
    - Shadow: no shadow file or reference is left under `HeishaMon/src/control`, `tests/control`, `docs/control` or the `Makefile`.
    - Docs: `firmware.md`, `loop.md`, `settings.md` and `device-test.md` match the code. This includes the round-1 fixes: "four input values" and "8 entries, plus 3 for each configured room".

    Earlier tests (group 6). All are unchanged since round 0, and each was changed or deleted under the leave the story's Interface gives with the human's word:
    - `tests/control/test_dispatch.c` (M): the PV rows are deleted.
    - `tests/control/test_form.c` (M): the removed name is replaced by `room.0.topic`, and one index shifts.
    - `tests/control/test_form_rows.c` (M): the removed name is replaced by `room.0.topic`.
    - `tests/control/test_html.c` (M): the removed name is replaced by `room.0.topic`.
    - `tests/control/test_settings.c` (M): the rows for the removed settings are deleted, and counts and indexes shift. Kept checks moved to `internal_w` or `room.0.topic`, and none was loosened.
    - `tests/control/test_settings_remote.c` (M): the kept-setting pair `pv_topic` is now `desired_indoor_c`.
    - `tests/control/test_telemetry.c` (M): the `inputs/solar_w` row and its fixture line are deleted, and counts shift.
    - `tests/control/test_shadow.c` and `tests/control/device/test_shadow.py` (D): shadow-only tests, which the story allows deleting.
    - `tests/control/device/shadow_host.c`, `shadow.py`, `shadow/shadow.ino`, `shadow.example.json` and `data/shadow_capture.jsonl` (D): the shadow tool and its data, which the Description names.

    Open, for the human:
    - The `## Description` still shows the original criterion 1. The builder's rewrite was left out because the work item is outside the builder's write paths. The binding reading is the human's, in entry 4.
    - `CTL_INPUT_PV`, `ctl_solar` and `ctl_loop_format_record` stay in the loop as the human decided in entry 4. Removing them would be a possible follow-up story.
