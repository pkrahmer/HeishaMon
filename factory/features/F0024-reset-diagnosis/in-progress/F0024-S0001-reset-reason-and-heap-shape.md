---
stage: approval
blocked: null
round: 1
attempts: 1
crashes: 0
pull_request: 42
comments_read: []
handed_over: approval
claim: null
journal:
- number: 1
  speaker: human
  kind: created
  at: '2026-11-15T21:58:13.370648Z'
  stage: doing
- number: 2
  speaker: factory
  kind: started
  at: '2026-11-15T21:58:13.376921Z'
  stage: doing
- number: 3
  speaker: builder
  kind: result
  at: '2026-11-15T22:10:07.105967Z'
  stage: doing
  run: d73fadc2dac9462a88555ee7834c74fd
- number: 4
  speaker: factory
  kind: started
  at: '2026-11-15T22:10:15.289778Z'
  stage: review
- number: 5
  speaker: reviewer
  kind: result
  at: '2026-11-15T22:11:39.423361Z'
  stage: review
  run: 07b90b07f67741769292028923d7c461
- number: 6
  speaker: factory
  kind: started
  at: '2026-11-15T22:11:44.397861Z'
  stage: doing
- number: 7
  speaker: builder
  kind: result
  at: '2026-11-15T22:18:00.057148Z'
  stage: doing
  run: d9c182cc4f734c26b40698021f97cc50
- number: 8
  speaker: factory
  kind: stall
  at: '2026-11-15T22:18:00.177999Z'
  stage: doing
  run: d9c182cc4f734c26b40698021f97cc50
  reason: check_red
- number: 9
  speaker: factory
  kind: started
  at: '2026-11-15T22:18:09.044180Z'
  stage: doing
- number: 10
  speaker: builder
  kind: result
  at: '2026-11-15T22:22:16.104133Z'
  stage: doing
  run: ac9a699dee074ea3aa264d082963580a
- number: 11
  speaker: factory
  kind: started
  at: '2026-11-15T22:22:21.392851Z'
  stage: review
- number: 12
  speaker: reviewer
  kind: result
  at: '2026-11-15T22:23:11.545053Z'
  stage: review
  run: 140ba8f594c64c71b06aed2680e725a0
---

# The reset reason and the heap's shape

## Description

**Why.** The owner's production board restarts when the HeishaMon main page is loaded on it. Without a serial cable on the board, the crash message is lost. The ESP8266 keeps the reason of its last reset across the reboot (`ESP.getResetInfoPtr()`: reason, exception cause, `epc1`, `epc2`, `epc3`, `excvaddr`, `depc`), and the heap's largest free block and fragmentation are available at run time (`ESP.getMaxFreeBlockSize()`, `ESP.getHeapFragmentation()`). This story publishes them, so that the next crash tells its cause.

**The reset reason.** Once per boot, after the first MQTT connection, the board publishes retained to `<base>/control/status/reset_reason` one line of text, for example `exception 28 epc1=0x4020f3a4 epc2=0x00000000 epc3=0x00000000 excvaddr=0x00000004 depc=0x00000000` or `power on`, `hardware watchdog`, `software watchdog`, `software restart`, `deep sleep wake`, `external reset`. The addresses are hexadecimal with eight digits. It is published once and not repeated on a later reconnect. On the ESP32-S3 the line is the reset reason the framework reports (`esp_reset_reason()`), named in the same plain words; fields the ESP32 does not offer are left out.

**The heap's shape.** With the other status values, the board publishes `<base>/control/status/heap_free` (bytes), `<base>/control/status/heap_max_block` (bytes, the largest block that can be allocated) and, on the ESP8266, `<base>/control/status/heap_fragmentation` (percent, 0 to 100).

**What stays.** The control loop, its settings and every other value: nothing changes but these topics.

## Interface

- The text of the reset reason is built by a pure function in a `.c` file of the glue, from the reason's fields as numbers, so that a host test checks it; reading the fields and publishing live in `glue/ctl_firmware.cpp`, with `#ifdef` for the two boards.
- Tests: a new host test `tests/control/test_reset_reason.c`. The leave for tests another story wrote: `tests/control/test_telemetry.c` may gain rows for the new status values, with indexes and counts shifted; no check may be removed or loosened.
- No upstream file changes, and no hook line changes.

## Acceptance criteria

1. The text function, rows of one test: reason 2 (exception) with cause 28, `epc1` 0x4020f3a4 and `excvaddr` 4 gives `exception 28 epc1=0x4020f3a4 epc2=0x00000000 epc3=0x00000000 excvaddr=0x00000004 depc=0x00000000`; reasons 0, 1, 3, 4, 5 and 6 give `power on`, `hardware watchdog`, `software watchdog`, `software restart`, `deep sleep wake` and `external reset`; an unknown reason 9 gives `reason 9`.
2. The text never exceeds a fixed buffer the header names, and the function refuses a buffer that is too small.
3. The telemetry publishes `status/heap_free`, `status/heap_max_block` and `status/heap_fragmentation` with the other status values.
4. `make firmware` builds both boards; report the D1 mini's global RAM and flash before and after.
5. `git diff develop --stat -- HeishaMon ':!HeishaMon/src'` shows no upstream file changed.
6. `make check` stays green.
7. docs: `docs/control/firmware.md` describes the topics, when they are published, and how to decode an exception address with the release ELF (`xtensa-lx106-elf-addr2line -pfiaC -e <elf> <epc1>`); `docs/control/decisions.md` gets one entry: the reset reason published retained once per boot, for diagnosing restarts without a serial cable.

## Smoke test

```sh
make test 2>&1 | grep -E '^control: [0-9]+ test programs passed$' | wc -l
```

Expect: `1`

## Journal

1. 2026-11-15 human: created.
2. factory: started doing as heishamon.
3. builder: Built the reset reason and the heap's shape.

   What I built:
   - `HeishaMon/src/control/glue/ctl_reset_reason.h` / `.c` (new, pure C99): `ctl_reset_info` (reason, exccause, epc1, epc2, epc3, excvaddr, depc as uint32_t), `ctl_reset_reason_esp8266(info, text, size)` and `ctl_reset_reason_esp32(reason, text, size)`, and `CTL_RESET_REASON_TEXT_MAX` = 105. That is the longest possible text (a ten-digit cause and five full addresses) plus its terminating zero, and a test checks that it matches exactly. The text is built with the existing `ctl_text` writer. A buffer too small for the text is refused and nothing is written to it, following the pattern of `ctl_telemetry`.
   - `glue/ctl_telemetry.h` / `.c`: the source gains `heap_free`, `heap_max_block`, `has_heap_fragmentation` and `heap_fragmentation`. Three status entries are added after `status/desired_source`: `status/heap_free`, `status/heap_max_block` and `status/heap_fragmentation`. The status count is 16, plus 1 when fragmentation is reported.
   - `glue/ctl_firmware.cpp`, with `#if defined(ESP8266)` / `ESP32` for the two boards:
     - `sample_heap()` reads the heap at each decision: `ESP.getFreeHeap`, then `getMaxFreeBlockSize` and `getHeapFragmentation` on the ESP8266, or `getMaxAllocHeap` on the ESP32.
     - `ctl_firmware_last_decision` hands the heap values to the source.
     - `publish_reset_reason()` runs in `ctl_firmware_subscribe`, ahead of the settings, so the diagnosis never waits behind them. It reads `ESP.getResetInfoPtr()` or `esp_reset_reason()` and publishes retained to `status/reset_reason`.
     - `publish()` now returns whether the client took the message. The once-per-boot flag is set only when the publish succeeded.
     - On the ESP32, a `static_assert` checks that `esp_reset_reason_t` is numbered the way the text table reads it.
     - The comment of `ctl_firmware_subscribe` in `ctl_firmware.h` is updated.
   - Tests:
     - `tests/control/test_reset_reason.c` (new): the rows of criterion 1, every field in its place, the 16 ESP32 names plus an unknown one, the longest text against the named constant, and refusals at exact size − 1 and at size 0 with the buffer left untouched.
     - `tests/control/test_telemetry.c`, within the leave the story gives:
       - The fixture sets the heap fields.
       - Three status rows are added.
       - The status count goes 14 → 17.
       - The refused index goes 14 → 17.
       - A new `test_heap_without_fragmentation` checks the ESP32 case: count 16, no fragmentation entry, index 16 refused.
       - No check was removed or loosened.
   - Docs:
     - `docs/control/firmware.md`: a new section *The reset reason* (topic, when it is published, both boards' words, the exception causes, decoding with `xtensa-lx106-elf-addr2line -pfiaC -e <elf> <epc1>` and the release ELF). The telemetry status table gains the three heap rows and the new count, and hook 5 mentions the reset reason.
     - `docs/control/decisions.md`: one entry for the reset reason published retained once per boot.

   Decisions where the story left room:
   - ESP32 words: `unknown`, `power on`, `external reset`, `software restart`, `exception` (a panic; the ESP32 keeps no exception fields), `interrupt watchdog`, `task watchdog`, `other watchdog`, `deep sleep wake`, `brownout`, `sdio reset`, `usb reset`, `jtag reset`, `efuse error`, `power glitch`, `cpu lockup`. Any other reason is `reason <n>`.
   - On the ESP32 the fragmentation entry is left out of the list rather than published as 0.
   - The heap is sampled at each decision, like the rest of the status entries, rather than read live when the page is built.
   - If the client does not take the reset reason, it is tried again at the next connect. Rejected alternatives: publishing on every connect; adding addresses to watchdog resets (the story fixes those texts).

   Results:
   - `make check` is green: lint, test (35 control programs), firmware for both boards. One earlier run of the ESP32 build died with an arduino-cli `internal compiler error: Segmentation fault` inside upstream's `HeishaMon/src/rules/rules.cpp`; the same tree then passed on the next run unchanged, so it looks like a toolchain flake.
   - Smoke test: `make test … | grep -E '^control: [0-9]+ test programs passed$' | wc -l` gives `1`, as expected.
   - Criterion 5: `git diff develop --stat -- HeishaMon ':!HeishaMon/src'` is empty. No upstream file and no hook line changed.
   - Criterion 4, D1 mini: develop was built from a `git archive` copy in a temp folder (that copy's ESP32 build hit the same compiler crash; only the D1 numbers were needed from it).
     - Global RAM: 48596 before → 49028 after (+432 bytes).
     - Flash code: 758184 → 758872 (+688 bytes).
     - ESP32-S3 after: sketch 1707003 bytes, globals 67616 bytes.

   Noticed, not touched: the RAM increase comes mostly from string literals (reset-reason names, the new subtopics). On the ESP8266, `.rodata` from plain C sits in RAM, and the pure `.c` files cannot use PROGMEM. If RAM gets tight, the ESP8266 name table could move into the `.cpp` as flash strings, at the cost of host coverage.

   `check`: check: green
4. factory: started review as heishamon.
5. reviewer: Findings:

   1. `HeishaMon/src/control/glue/ctl_telemetry.h:40`. The comment on `ctl_telemetry_count` says "16 status entries and one more with the heap's fragmentation". It never says the 17th entry depends on `has_heap_fragmentation`, so a reader can take the count to be always 17. The line is also 131 columns long, while the rest of the header wraps at 100. Fix: say "16 status entries, 17 when the source has the heap's fragmentation", and wrap the line at 100 columns.
   2. Story gap, `HeishaMon/src/control/glue/ctl_firmware.cpp` `publish_reset_reason()` (around line 367). The Description says the reset reason "is published once and not repeated on a later reconnect", and the build adds "retried when the client did not take it". No criterion covers either rule, and no test checks them: the once-per-boot latch lives only in the `.cpp`, which the host tests do not build. A latch that never sets, or one that sets even when the publish fails, would pass `make check`. Fix, one of two: the builder moves the latch decision into a pure function in a `.c` file of the glue (input: already published and publish taken; output: publish now or not, and the new state) and asks the tester for a host test of both sides; or the human states that this rule is checked only on the board. I would choose the first.

   Checked, no finding:
   - Criterion 1: every row, plus every field in its place, is checked by `tests/control/test_reset_reason.c`. Non-exception rows also carry a cause and an `epc1`, so a check would catch fields that leak into those texts.
   - Criterion 2: the longest text equals `CTL_RESET_REASON_TEXT_MAX` (105 = 104 + 1, recounted). Both sides of the size limit are tested for each board: a buffer of exactly the text's size is taken, one byte less is refused, and size 0 is refused, each time with the buffer left untouched.
   - Criterion 3: the three rows and the counts 17 and 16 are tested. An index past the end is refused through `ctl_telemetry_count`, so the `default:` case only ever sees index 16.
   - Criterion 5: `git diff develop...HEAD --stat -- HeishaMon ':!HeishaMon/src'` is empty.
   - Criteria 4 and 6: carried by the journal (`check: green`).
   - ESP8266 reason numbering matches the SDK's `rst_reason`. The ESP32 table matches `esp_reset_reason_t` 0 to 15, with a partial `static_assert`.
   - Architecture and embedded safety: no dynamic memory, loops are bounded, table access is bounded by `sizeof`, uint32 values reach `ctl_format_fixed` exactly, the private names are `static`, and the new `.c` file is pure C99 that reuses `ctl_text` rather than copying a helper.
   - Docs: `docs/control/firmware.md` and `docs/control/decisions.md` match the code: the topics, when the reason is published, both boards' words, 105 bytes, the 16/17 count, the `addr2line` line, and the Xtensa causes 0, 3, 28 and 29.
   - The control page (`ctl_web.cpp:138,189`) builds its rows from `ctl_telemetry_count`, so it gains the three heap rows on its own. That fits "nothing changes but these topics" as long as the page counts as showing the topics.

   Earlier tests:
   - `tests/control/test_telemetry.c` (M): the story's Interface names it, with its leave to add rows and shift indexes and counts. The status count went 14→17 and the refused index 14→17, three rows and `test_heap_without_fragmentation` were added, and no check was removed or loosened.

   Notes:
   - `tests/control/test_reset_reason.c:76`: the ESP8266 unknown reason is tested only at 9, not at 7, the first number past the table. The shared `put_name` bound is still covered by the ESP32 row 16.
6. factory: started doing as heishamon.
7. builder: Round 1: I fixed both review findings and changed nothing else.

   1. Telemetry count comment: in `HeishaMon/src/control/glue/ctl_telemetry.h`, the comment on `ctl_telemetry_count` now reads "16 status entries, 17 when the source has the heap's fragmentation". It is wrapped at 100 columns.
   2. The once-per-boot rule had no test. I took the reviewer's first option and moved the decision into pure code.
      - New in `HeishaMon/src/control/glue/ctl_reset_reason.h` / `.c`:
        - `ctl_reset_latch` (one `bool published`) with `CTL_RESET_LATCH_INIT`.
        - `ctl_reset_latch_due(latch)` is true until a publish has been taken.
        - `ctl_reset_latch_record(latch, taken)` sets the latch only when the publish was taken. Once set, it stays set for the boot.
      - `glue/ctl_firmware.cpp`: `publish_reset_reason()` now uses the latch (`reset_reason_latch`) in place of the bare `static bool`. The behaviour is the same as before.
      - I wrote the tests first and saw them fail to compile. They are in `tests/control/test_reset_reason.c`:
        - `test_latch_is_due_until_a_publish_is_taken`: the latch is still due after refused publishes and ends after a taken one.
        - `test_latch_stays_set_for_the_boot`: once a publish is taken, a later refused or taken one does not make it due again.
      - `docs/control/firmware.md`, section *The reset reason*: one sentence names the latch and its two sides.
      - I did not add a new entry to `docs/control/decisions.md`: the existing entry already covers the rule.

   Rejected: two bare functions on a `bool` (with a struct, the state is one named thing), and leaving the rule checked only on the board (option two needed the human's word).

   Not addressed: the reviewer's note that the ESP8266 unknown reason is tested only at 9 and not at 7. It was a note, not a finding.

   Results:
   - `make check`: green. lint is green; test is green (36 upstream rules tests, 35 control programs); firmware builds both boards.
   - Smoke test: the command gives `1`, as expected.
   - Criterion 5: `git diff develop --stat -- HeishaMon ':!HeishaMon/src'` is empty.
   - Criterion 4, D1 mini, develop → this round:

   | | develop | round 0 | this round |
   |---|---|---|---|
   | Global RAM (bytes) | 48596 | 49028 | 49028 |
   | Flash code (bytes) | 758184 | 758872 | 758904 |

   - ESP32-S3 this round: sketch 1707059 bytes, globals 67616 bytes.
8. factory: Stage doing stalled (attempt 1 of 2): a check is red after the stage: `check`
   tests/control/test_types.c:26: got 1, expected 2
   tests/control/test_types.c:27: got 0x1, expected 0x2
   tests/control/test_types.c:28: got "one", expected "two"
   test_types: the next two lines are expected failures of the check helper
   tests/control/test_types.c:49: got 1, expected 1.0000000000000002
   tests/control/test_types.c:52: got nan, expected 0
   control: 35 test programs passed
   test: green (17.7 s)
   esp8266:
   . Variables and constants in RAM (global, static), used 49028 / 80192 bytes (61%)
   . Code in flash (default, ICACHE_FLASH_ATTR), used 758904 / 1048576 bytes (72%)
   /opt/arduino/data/packages/esp32/tools/esp-x32/2601/xtensa-esp-elf/include/c++/14.2.0/cmath:2641:63: internal compiler error: Segmentation fault
   0x1888336 internal_error(char const*, ...)
   Error during build: exit status 1
   make: *** [Makefile:88: firmware] Error 1
9. factory: started doing as heishamon.
10. builder: Round 1, second run: no code changes. The stall came from the toolchain, not from the work.

    The stall in entry 8 was `internal compiler error: Segmentation fault` in the ESP32 toolchain's own `xtensa-esp-elf/include/c++/14.2.0/cmath`, inside the ESP32 firmware build. Lint and test were green in that run, and so was the D1 mini build. This is the same arduino-cli/GCC crash recorded in entry 3, when it hit upstream's `rules.cpp`. It does not depend on this story's code: the committed tree is the one that went green in entry 7.

    What I did:
    - `git status` is clean: the round 1 fixes from entry 7 are committed, and I made no edits.
    - `make check`, run again in the foreground on the unchanged tree: green.
      - lint: green.
      - test: green, 35 control programs.
      - firmware: green for both boards.
        - D1 mini: global RAM 49028 bytes, flash code 758904 bytes, the same as entry 7.
        - ESP32-S3: sketch 1707059 bytes, globals 67616 bytes.
      - The last line was `check: green`.
    - Smoke test as written: it gives `1`, as expected.
    - Criterion 5: `git diff develop --stat -- HeishaMon ':!HeishaMon/src'` is empty.

    What I built, unchanged from entries 3 and 7:
    - `HeishaMon/src/control/glue/ctl_reset_reason.h` and `.c`: the reset-reason text and the once-per-boot latch.
    - `glue/ctl_telemetry.h` and `.c`: the three heap status entries.
    - `glue/ctl_firmware.cpp`: reads the reset reason and the heap on each board and publishes them.
    - Tests: `tests/control/test_reset_reason.c`, and the leave the story gives for `tests/control/test_telemetry.c`.
    - Docs: `docs/control/firmware.md` and `docs/control/decisions.md`.

    Noticed, not touched: the ESP32 compiler crash has now hit the build in 3 runs, on different files. The factory's check can therefore fail on a tree that is fine. Retrying fixes it. A lasting fix would be in the toolchain or the `Makefile`, both outside this stage's write paths.

    `check`: check: green
11. factory: started review as heishamon.
12. reviewer: no findings

    Round 1 review. Both findings from entry 5 are fixed:
    - `HeishaMon/src/control/glue/ctl_telemetry.h:39-41`: the comment on `ctl_telemetry_count` now says "16 status entries, 17 when the source has the heap's fragmentation", which matches `STATUS_COUNT + (has_heap_fragmentation ? 1 : 0)` in `ctl_telemetry.c`.
    - The once-per-boot rule is now the pure `ctl_reset_latch` (`ctl_reset_latch_due` and `ctl_reset_latch_record` in `glue/ctl_reset_reason.h` and `.c`). `publish_reset_reason()` in `ctl_firmware.cpp` gates on it and records the bool that `publish()` returns. Both sides have host tests: `test_latch_is_due_until_a_publish_is_taken` checks that refused publishes leave it due and a taken one ends it, and `test_latch_stays_set_for_the_boot` checks that a later refused or taken publish does not make it due again.

    What I checked in every group:
    1. Acceptance criteria:
       - Criterion 1: every row is in `test_exception` and `test_plain_reasons`, including reason 9. The non-exception rows carry a cause and an `epc1`, so leaked fields would show.
       - Criterion 2: `test_longest_text_fits` checks that the longest text equals `CTL_RESET_REASON_TEXT_MAX` (105). `test_small_buffer_is_refused` checks the exact size and size − 1 on both boards, and size 0, with the buffer left untouched.
       - Criterion 3: the three rows are in `test_entries`. The count is 17 with fragmentation and 16 without, and the refused index is 17 or 16.
       - Criterion 5: `git diff --name-status develop...HEAD` lists only files under `HeishaMon/src/control/`, `docs/control/` and `tests/control/`.
       - Criteria 4 and 6: the journal reports `check: green` in entry 10 and the D1 mini's before and after numbers. The red check in entry 8 was an internal compiler error in the ESP32 toolchain's `cmath`, not in this diff.
    2. Architecture:
       - `ctl_reset_reason.c` is pure C99 and includes only its own header, `loop/ctl_text.h` and standard headers.
       - The Arduino code stays in the `.cpp`. Public names are `ctl_`/`CTL_`, helpers are `static`, and the refusals return a status.
       - No upstream file or hook line changed.
    3. Embedded safety:
       - No dynamic memory and no recursion. Loops are bounded (`HEX_DIGITS`, the row tables).
       - Name lookups are bounded by `sizeof` of their tables. The text is built in a buffer of `CTL_RESET_REASON_TEXT_MAX` and copied out only when `length < size`.
       - The heap values are uint32 from the SDK, with no NaN path.
    4. Quality: no dead code. The text writer reuses `ctl_text` rather than copying it, and the comments give reasons.
    5. Hygiene:
       - The journal records the main decisions and what was rejected, and the Description matches the build.
       - `docs/control/firmware.md` matches the code: the topic, when it is published, the retry when the client does not take it, the words for both boards, 105 bytes, the 16/17 count and the `addr2line` line.
       - `docs/control/decisions.md` has the one entry, with its decision, reason and what was rejected.
    6. Earlier tests: `tests/control/test_telemetry.c` (M) was changed under the story's Interface leave:
       - The fixture gains the heap fields.
       - Three status rows are added.
       - The status count goes 14→17 and the refused index 14→17.
       - `test_heap_without_fragmentation` is new.
       - No check was removed or loosened.

    Notes:
    - `tests/control/test_reset_reason.c:76`: the ESP8266 unknown reason is still tested only at 9, not at 7, the first number past the table.
    - `HeishaMon/src/control/glue/ctl_reset_reason.h:49,52` and `ctl_firmware.h:40` are 101 columns wide. Nothing enforces a width, and other control headers are wider.
    - `docs/control/firmware.md:171`: "in the same header" comes before the header is named; `glue/ctl_reset_reason.h` is named in the next paragraph.
