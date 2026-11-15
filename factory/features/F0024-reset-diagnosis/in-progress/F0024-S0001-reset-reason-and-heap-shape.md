---
stage: doing
blocked: null
round: 1
attempts: 0
crashes: 0
pull_request: 42
comments_read: []
handed_over: null
claim:
  instance: heishamon
  stage: doing
  started_at: '2026-11-15T22:11:44.397861Z'
  lease_duration: 60
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
