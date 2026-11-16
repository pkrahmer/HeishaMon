---
stage: approval
blocked: null
round: 1
attempts: 0
crashes: 0
pull_request: 45
comments_read: [6903522919]
handed_over: approval
claim: null
journal:
- number: 1
  speaker: human
  kind: created
  at: '2026-11-15T23:37:47.579942Z'
  stage: doing
- number: 2
  speaker: factory
  kind: started
  at: '2026-11-15T23:37:47.586738Z'
  stage: doing
- number: 3
  speaker: builder
  kind: result
  at: '2026-11-15T23:49:54.961265Z'
  stage: doing
  run: 298accfb5b054ca6a97c2fb59a20b797
- number: 4
  speaker: factory
  kind: started
  at: '2026-11-15T23:50:04.504551Z'
  stage: review
- number: 5
  speaker: reviewer
  kind: result
  at: '2026-11-15T23:51:52.258980Z'
  stage: review
  run: e67ea9a5332b446d826348568fdfa94e
- number: 6
  speaker: factory
  kind: started
  at: '2026-11-15T23:51:58.063034Z'
  stage: doing
- number: 7
  speaker: builder
  kind: question
  at: '2026-11-15T23:55:13.983118Z'
  stage: doing
  run: bb640a8b359d416dbb76fb5cb2856bc2
  blocked: question
- number: 8
  speaker: human
  kind: comment
  at: '2026-11-15T23:57:02.428198Z'
  stage: doing
- number: 9
  speaker: factory
  kind: started
  at: '2026-11-15T23:57:05.143165Z'
  stage: doing
- number: 10
  speaker: builder
  kind: result
  at: '2026-11-16T00:01:52.768538Z'
  stage: doing
  run: 8255afe0524041b9a9bd86ab904408d4
- number: 11
  speaker: factory
  kind: started
  at: '2026-11-16T00:02:00.029016Z'
  stage: review
- number: 12
  speaker: reviewer
  kind: result
  at: '2026-11-16T00:03:25.801699Z'
  stage: review
  run: 294d9263143143d9b2918f6a51c65da8
---

# Fewer room slots and shared buffers

## Description

**Why.** After S0001, the control code's largest RAM items are its fixed tables and buffers: the settings hold 16 room slots with a 96-character topic each (about 1.9 KB), and the firmware glue (`glue/ctl_firmware.cpp`) keeps several separate scratch texts (log line, topic, subtopic, value, setting value, integral text) that are never all in use at the same time. The owner uses 5 rooms. The web glue (`glue/ctl_web.cpp`) stays as it is: its form report and the held desired-setpoint topic are state, not scratch, and the page's buffers are already on the stack.

**What changes.** `CTL_ROOMS_MAX` goes from 16 to 8, and every table sized by it shrinks with it. A message or a form that sets `rooms` above 8, or a room index of 8 or more, is refused like any out-of-range value; a `/control.json` with more rooms loads the first 8 and refuses the rest. The firmware glue's scratch texts that are never in use at the same time share storage; each use keeps its own size limit, and no text is cut shorter than today.

**What stays.** Every topic, value, setting, log text and page byte for up to 8 rooms; the core's behaviour; the golden traces.

## Interface

- `CTL_ROOMS_MAX` 8, with every dependent size.
- The shared scratch texts in `glue/ctl_firmware.cpp`; `glue/ctl_web.cpp` is unchanged.
- The leave for tests another story wrote: tests that use rooms 9 to 16, or that check the room limit or a count derived from it (`tests/control/test_rooms.c`, `tests/control/test_settings*.c`, `tests/control/test_form*.c`, `tests/control/test_telemetry.c`, `tests/control/test_html.c`, `tests/control/test_lean_page.c`), may change their room numbers, limits and counts to the new limit; no other check may be removed or loosened.
- No upstream file changes, and no hook line changes.

## Acceptance criteria

1. `make firmware` builds both boards, and the D1 mini's global RAM is at least 3,500 bytes below 49,108 bytes (the feature's baseline). Report the number.
2. Setting `rooms` to 8 is accepted and to 9 refused; `room.7.topic` is accepted and `room.8.topic` refused; a `/control.json` with 10 rooms loads the first 8 and refuses the rest.
3. The control page and the telemetry for 5 rooms are byte for byte as before.
4. Every golden trace replays bit for bit.
5. `git diff develop --stat -- HeishaMon ':!HeishaMon/src'` shows no upstream file changed.
6. `make check` stays green.
7. docs: `docs/control/settings.md` says the room limit is 8; `docs/control/firmware.md` updates its RAM budget section; `docs/control/decisions.md` gets one entry: 8 room slots and shared buffers, to give the web server RAM; rejected: 16 slots (two thirds of them unused on the owner's board).

## Smoke test

```sh
make test 2>&1 | grep -E '^control: [0-9]+ test programs passed$' | wc -l
```

Expect: `1`

## Journal

1. 2026-11-15 human: created.
2. factory: started doing as heishamon.
3. builder: Built: 8 room slots and shared scratch buffers. `make check` is green. The D1 mini's global RAM is now **45,060 bytes**. That is 4,048 below the 49,108 baseline (the target was at least 3,500) and 1,424 below the 46,484 that S0001 left.

   What changed (paths):
   - `HeishaMon/src/control/adapt/ctl_rooms.h`: `CTL_ROOMS_MAX` is 8. The tables sized by it shrink with it: the settings' topics and weights, the rooms' configuration and state, `CTL_SETTINGS_NAMES_MAX`, the form and the form report.
   - `HeishaMon/src/control/glue/ctl_settings.{h,c}`: new `ctl_settings_load(settings, name, value)`. It does what `ctl_settings_set` does, with one exception: a whole `rooms` count above 8 is taken as 8. So a `/control.json` with 10 rooms loads rooms 0–7 and refuses `room.8.*` and `room.9.*` as UNKNOWN pairs, which are logged and skipped.
   - `HeishaMon/src/control/glue/ctl_firmware.cpp`: `load_settings` now calls `ctl_settings_load`. The scratch texts share storage in two unions:
     - one holds the value texts: published value 128, setting value 100, integral text 24;
     - the other holds the MQTT topic and subtopic (192 + 48) and the log line (160).
     
     Each old name is kept as a reference to its own union member, so every `sizeof` limit is unchanged and no text is cut shorter. I checked every use site to confirm the pairs are never in use at the same time. A log line can show a value text, so the log line never shares with values. A topic is built while its value waits to be published, so topics and values stay apart too.
   - Tests:
     - New `tests/control/test_room_limit.c` covers criterion 2: rooms 8 accepted and 9 refused; `room.7.topic` accepted and `room.8.topic` refused; the 10-room file loads 8 rooms. It also checks that `ctl_settings_load` gives the same result as the setter for every other input.
     - New `tests/control/test_five_rooms.c` with `tests/control/data/five_rooms.txt` covers criterion 3. It holds every telemetry message, page row and settings form row for 5 rooms. I generated the reference with the 16-slot code before the change, and it still matches byte for byte after it.
     - Adjusted under the story's leave: `test_rooms.c` (counts 17→9 and 16→8), `test_settings.c` (rooms 16/17→8/9, `room.16`→`room.8`, `room.15`→`room.7`) and `test_telemetry.c` (`room/15`→`room/7`). No other check was removed or loosened.
   - Docs:
     - `docs/control/settings.md`: rooms 0 to 8; a file with more rooms keeps its first 8.
     - `docs/control/firmware.md`: the RAM budget section now covers room slots, the buffer-sharing rules and the RAM figures. I also fixed the `rooms` row ("0 to 8") and the settings-file paragraph, which now names `ctl_settings_load` (the same document, made true again).
     - `docs/control/decisions.md`: a new entry.

   Decisions and rejected options:
   - The count is capped only when reading the file. A message or form asking for 9 rooms is still refused, as the story says, so the setter does not correct it. A file with more rooms must not fall back to the defaults, because that would disable the loop.
   - I did not use macros for the shared names: they would hide where the storage lives. I did not prefix every use with the union member either: it lengthens every line.
   - I did not use one union for all scratch texts, because of the overlaps described above.
   - `ctl_web.cpp` is unchanged. The story lists a desired-setpoint buffer and a form report buffer there, but neither is scratch:
     - `last_report` must live from the save to the next page;
     - the held setpoint's topic (`ctl_desired.topic`, held in `ctl_firmware.cpp`) is state for as long as the board runs;
     - the page's chunk and row buffers are already on the stack.
     
     Sharing any of these would break behaviour. `last_report` still shrinks with the name count, and the RAM target is met without further changes.

   Criteria:
   1. Met (45,060 bytes, both boards build).
   2. Met: covered by `test_room_limit.c`.
   3. Met: covered by `test_five_rooms.c`.
   4. Met: the golden traces pass in `make test`.
   5. Met: `git diff develop --stat -- HeishaMon ':!HeishaMon/src'` shows nothing.
   6. Met: `make check` is green.
   7. Met: the three documents are updated.

   Smoke test: `make test 2>&1 | grep -E '^control: [0-9]+ test programs passed$' | wc -l` gives `1`, as expected (40 programs passed).

   Noticed, not touched:
   - `docs/control/adapters.md:80` still says "`CTL_ROOMS_MAX` (16)". The docs criterion does not name that file, so I left it; it needs a one-word fix.
   - Older entries in `decisions.md` mention 16 rooms as history, and I left them.

   `check`: check: green
4. factory: started review as heishamon.
5. reviewer: Findings:

   1. `## Description` and `## Interface` of the work item vs `HeishaMon/src/control/glue/ctl_web.cpp` (unchanged): the story says the web glue's desired-setpoint and form report buffers share storage, and the Interface lists "the shared buffers in `glue/ctl_web.cpp`". Nothing there was built. The builder's reason may well be right: `last_report` lives from the save to the next page, `ctl_desired.topic` is state, and the page's buffers are on the stack. But the Description was neither corrected with the human's word nor rewritten, so it no longer matches what was built. Fix: ask the human (R8/R13) whether to drop the web glue from the story. On their answer, rewrite the Description and Interface to match.
   2. `docs/control/adapters.md:80`: it still says "`CTL_ROOMS_MAX` (16)", which this change made false. Fix: say 8.
   3. `tests/control/test_five_rooms.c` `set_settings`/`render_settings`: criterion 3 says the control page is byte for byte as before. But the reference renders the settings form only with an empty report (`ctl_form_report_start`), so no row with a save result is ever compared. The tables this change shrank are `ctl_form` and `ctl_form_report`, sized by `CTL_FORM_FIELDS_MAX`/`CTL_SETTINGS_NAMES_MAX`. They feed exactly those rows, and the save path with them is not exercised for 5 rooms. Fix: in the render, post and save a 5-room form through `ctl_form`'s parse and save with the settings setter, so that the report holds at least one saved, one refused and one unchanged setting. Render the rows with that report, and regenerate `tests/control/data/five_rooms.txt` from `develop`'s 16-slot code.

   Checked and found sound (no finding):
   - Every use site of the two unions in `ctl_firmware.cpp`. No value text is live across a write of another value text. No topic or subtopic is live across `log_text`. `store_integral` keeps `integral_text` until `write_integral` reads it, and log lines only follow a failure.
   - `ctl_settings_load` gives the setter's answers for NULL and malformed input, and `test_room_limit.c` covers both sides of 8/9, `room.7`/`room.8` and the 10-room file.
   - `git diff --name-status develop...HEAD` shows no file outside the fork's folders and the work item.
   - The factory's `make check` after the builder was green.

   Earlier tests:
   - `tests/control/test_rooms.c`: count limits 17→9 and 16→8, under the story's named leave (`test_rooms.c`).
   - `tests/control/test_settings.c`: rooms 16/17→8/9 and `room.16`/`room.15`→`room.8`/`room.7`, under the story's named leave (`test_settings*.c`).
   - `tests/control/test_telemetry.c`: `room/15`→`room/7`, under the story's named leave (`test_telemetry.c`).
6. factory: started doing as heishamon.
7. builder: Round 1. I fixed findings 2 and 3. Finding 1 needs your word, so I am stopping here with a question.

   **Question:** should I drop `glue/ctl_web.cpp` from the story's Description and Interface?

   The Description says the web glue's "desired-setpoint and form report buffers" share storage, and the Interface lists "the shared buffers in `glue/ctl_web.cpp`". Neither one is scratch:
   - `last_report` (the form report) has to survive from a save until the next page render.
   - The desired-setpoint topic (`ctl_desired.topic`) is state that the firmware holds for as long as the board runs.
   - The page's chunk and row buffers are already on the stack.

   Putting any of these in the same storage as another buffer would change behaviour. The RAM target is met without them: the D1 mini uses 45,060 bytes against a target of at most 45,608. `last_report` also shrinks with the room limit.

   Options:
   - (a) Drop the web glue from the story. I would rewrite the Description's sentence to name only `glue/ctl_firmware.cpp`'s scratch texts and remove `glue/ctl_web.cpp` from the Interface. Code stays as it is. **My choice.**
   - (b) Keep it and say which buffers in `ctl_web.cpp` you want shared, knowing that sharing `last_report` would lose the save result shown on the next page.

   On your answer I will rewrite the Description and Interface to match, then hand forward to review.

   **Done this round (left in the working tree):**
   - Finding 2: `docs/control/adapters.md:80` now says "`CTL_ROOMS_MAX` (8)". The docs criterion does not name this file, but the reviewer asked for the fix.
   - Finding 3: in `tests/control/test_five_rooms.c`, a new `save_form` posts a 5-room form through `ctl_form_keep` and `ctl_form_apply`, using a setter built on `ctl_settings_set` that compares the value before and after. The form holds:
     - `room.4.weight=abc`: refused as a bad value
     - `room.2.weight=0.75`: saved
     - `room.1.topic` with its current value: unchanged
     - `rooms=5`
     
     The test checks those three results, then renders the settings rows with that report.
   - I regenerated `tests/control/data/five_rooms.txt` from `develop`'s 16-slot code. I exported `develop`'s control code and tests with `git archive` into a `mktemp` folder, built the new test there with a dump of the render, and confirmed `CTL_ROOMS_MAX` was 16. The new reference differs from the old one only in the `room.2.weight` lines (0.75, "saved") and the `room.4.weight` row ("refused: not a valid value"). The 8-slot code matches it byte for byte.

   **Checks:**
   - `make check` is green: lint, test and firmware.
   - Smoke test: `make test 2>&1 | grep -E '^control: [0-9]+ test programs passed$' | wc -l` gives `1`, as expected.

   **Open:** finding 1, the question above.
8. 2026-11-15 human: (a). You are right: `last_report` and the desired-setpoint topic are state, not scratch, and the page buffers are already on the stack. Drop `glue/ctl_web.cpp` from the Description and the Interface and name only `glue/ctl_firmware.cpp`'s scratch texts; the code stays as it is.

   Your fixes for findings 2 and 3 stand, including the regenerated `five_rooms.txt` reference. 45,060 bytes is well past the target, thank you.
9. factory: started doing as heishamon.
10. builder: Round 1, after the human's answer (a). I rewrote the work item's `## Description` and `## Interface` to drop the web glue, as the human asked. No code, test or document changed in this run.

    - **Why:** it now names only `glue/ctl_firmware.cpp`'s scratch texts: log line, topic, subtopic, value, setting value and integral text. It also says why `glue/ctl_web.cpp` stays as it is: the form report and the held desired-setpoint topic are state, not scratch, and the page's buffers are already on the stack.
    - **What changes:** "The glue's scratch buffers" now reads "The firmware glue's scratch texts".
    - **Interface:** "The shared scratch texts in `glue/ctl_firmware.cpp`; `glue/ctl_web.cpp` is unchanged."

    Fixes for findings 2 and 3 from the previous run are already committed and stand, as the human confirmed:
    - `docs/control/adapters.md:80` now says 8.
    - `tests/control/test_five_rooms.c` has the save path.
    - `tests/control/data/five_rooms.txt` is regenerated from `develop`'s 16-slot code.

    Checks:
    - `make check` is green: lint, test (40 control programs) and firmware.
    - The D1 mini's global RAM is 45,060 / 80,192 bytes, 4,048 below the 49,108 baseline. The ESP32-S3 builds and uses 66,192 bytes.
    - Smoke test: `make test 2>&1 | grep -E '^control: [0-9]+ test programs passed$' | wc -l` gives `1`, as expected.

    Decisions: none new. The decision log entry from the first run still says what was built. It covers only the firmware glue's buffers, so it needs no change.

    Noticed, not touched: older entries in `docs/control/decisions.md` mention 16 rooms as history.

    The factory left out changes outside the write paths: factory/features/F0026-give-ram-back/in-progress/F0026-S0002-fewer-rooms-shared-buffers.md.

    `check`: check: green
11. factory: started review as heishamon.
12. reviewer: no findings

    Work item: I rewrote the `## Description` and `## Interface` as the human asked in entry 8, answer (a). The builder tried to make this change in entry 10, but the factory dropped it because the work item is outside the builder's write paths (`factory/pipeline.yml`: only the reviewer may write `{work_item}`). The new text:
    - **Why:** names only `glue/ctl_firmware.cpp`'s scratch texts. It says `glue/ctl_web.cpp` stays as it is, because its form report and the held desired-setpoint topic are state, and the page's buffers are already on the stack.
    - **What changes:** now reads "A message or a form that sets `rooms` above 8 … is refused …; a `/control.json` with more rooms loads the first 8 and refuses the rest". The old sentence also said a `/control.json` setting rooms above 8 is refused, which contradicted criterion 2 and the code.
    - **Interface:** "The shared scratch texts in `glue/ctl_firmware.cpp`; `glue/ctl_web.cpp` is unchanged."

    No code, test or document changed in this run.

    Checked and found sound:
    - **Criteria.**
      - Criterion 2: `tests/control/test_room_limit.c` tests both sides of each limit. `rooms` 8 is accepted and 9 refused, both through the setter. `room.7.topic`/`weight` are accepted and `room.8.*` refused as UNKNOWN. A 10-room file through `ctl_settings_load` keeps rooms 0–7, refuses rooms 8 and 9, and stays valid.
      - `ctl_settings_load` gives the same answer as the setter for every other input (NULL, malformed, negative, fractional, `1e99`, other names).
      - Criterion 3: `test_five_rooms.c` compares against `five_rooms.txt`, which the builder generated from `develop`'s 16-slot code. It covers the telemetry, the page rows, and the settings rows with saved, refused and unchanged results.
      - Criterion 4: the golden traces replay in `make test`.
      - Criteria 1 and 6: entry 10 reports `make check` green and 45,060 bytes. The RAM figure is a build output, not a test.
    - **Architecture.** `git diff --name-status develop...HEAD` shows changes only in `HeishaMon/src/control/`, `tests/control/`, `docs/control/` and the work item. No upstream file and no hook line changed. The new function `ctl_settings_load` is in a `.c` file outside the core and is pure, and its header comment says what it returns and refuses.
    - **Embedded safety.**
      - Every use of the two unions in `ctl_firmware.cpp`:
        - `publish`/`publish_flash` use the topic and subtopic, which are separate struct members.
        - `log_text` never runs while a topic is live.
        - `handle_desired` and `handle_set_energy` log `value_text` from the other union.
        - `load_integral` and `store_integral` keep `integral_text` until `write_integral` reads it.
        - `write_settings` keeps `setting_value`, and `ctl_firmware_change` compares it with its own local `before`.
        - `ctl_firmware_mqtt` reads the setting's name from the dispatch's copy, not from scratch.
      - Each reference keeps its member's `sizeof`, and every write uses it.
    - **Docs.** `settings.md`, `firmware.md` (the `rooms` row, the settings-file paragraph and the RAM budget) and `adapters.md:80` now say 8 and `ctl_settings_load`, and the code matches. `decisions.md` has the entry, with date, decision, reason and what was rejected. No present-tense mention of 16 rooms is left outside the decision log's history.

    Earlier tests:
    - `tests/control/test_rooms.c`: count limits changed from 17/16 to 9/8, under the story's named leave (`test_rooms.c`).
    - `tests/control/test_settings.c`: `rooms` 16/17 → 8/9, `room.16` → `room.8`, `room.15` → `room.7`, under the story's named leave (`test_settings*.c`).
    - `tests/control/test_telemetry.c`: `room/15` → `room/7`, under the story's named leave (`test_telemetry.c`).

    Notes:
    - `HeishaMon/src/control/glue/ctl_settings.c:546`: a file count above `INT_MAX` (such as `"3000000000"`) is refused rather than taken as 8. A firmware that wrote the file never stores such a count, so I did not count this as a finding.
