---
stage: review
blocked: null
round: 1
attempts: 0
crashes: 0
pull_request: 44
comments_read: []
handed_over: null
claim: null
journal:
- number: 1
  speaker: human
  kind: created
  at: '2026-11-15T23:08:50.911905Z'
  stage: doing
- number: 2
  speaker: factory
  kind: started
  at: '2026-11-15T23:08:50.918021Z'
  stage: doing
- number: 3
  speaker: builder
  kind: result
  at: '2026-11-15T23:24:40.346333Z'
  stage: doing
  run: 313676d6918743abbcd5940be9af8c62
- number: 4
  speaker: factory
  kind: started
  at: '2026-11-15T23:24:48.195567Z'
  stage: review
- number: 5
  speaker: reviewer
  kind: result
  at: '2026-11-15T23:28:47.654910Z'
  stage: review
  run: 128f8d0dd4d74c3cb1fe9b9b13b3943b
- number: 6
  speaker: factory
  kind: started
  at: '2026-11-15T23:28:52.449310Z'
  stage: doing
- number: 7
  speaker: builder
  kind: result
  at: '2026-11-15T23:33:32.173768Z'
  stage: doing
  run: 0ec168aad0a740d08b49998c5c4595ec
---

# Constant text in flash

## Description

**Why.** On the ESP8266, a `const char` text lives in RAM unless it is placed in flash. The control code keeps about 4.0 KB of such text in RAM: topic and subtopic names, setting names, log texts, the control page's snippets, the reset reason words and the type names. Upstream's web server needs that RAM when it builds the main page; without it, the board restarts. This story moves the control code's constant text to flash on the ESP8266, as upstream does with `PSTR`, `F()` and `PROGMEM`, without changing any text or behaviour.

**How it stays pure.** The `.c` files must still compile on the host as strict C99. One small header in the control code provides what is needed: on the ESP8266 the flash attribute and flash-safe reading, on the host and the ESP32 plain `const` data and ordinary reading. Text read from flash is copied into the caller's buffer before it is used, as the glue already does with `ctl_text`.

**What stays.** Every text, topic, value, log line and page byte is exactly as before; the core's behaviour and the golden traces are untouched.

## Interface

- One new header under `HeishaMon/src/control/` with the flash macros and their host fallbacks; its use in the files that hold the constant text.
- No test another story wrote changes, except where a test reads a table directly and must now read it through the new accessor; such a change keeps the check's meaning and is named in the result.
- No upstream file changes, and no hook line changes.

## Acceptance criteria

1. `make firmware` builds both boards, and the D1 mini's global RAM ("Variables and constants in RAM") is at least 2,500 bytes below `develop` at this story's start. Report both numbers.
2. A host test renders the control page with a fixed settings set and compares it byte for byte with the rendering on `develop` (a reference captured into the test), and the telemetry test's topic and value texts stay as they are.
3. Every golden trace replays bit for bit.
4. `git diff develop --stat -- HeishaMon ':!HeishaMon/src'` shows no upstream file changed.
5. `make check` stays green.
6. docs: `docs/control/firmware.md` gets a short section on the RAM budget: why constant text lives in flash on the ESP8266, and the rule for new text; `docs/control/decisions.md` gets one entry: constant text in flash, because the board restarted when the main page ran out of heap; rejected: leaving it in RAM.

## Smoke test

```sh
make test 2>&1 | grep -E '^control: [0-9]+ test programs passed$' | wc -l
```

Expect: `1`

## Journal

1. 2026-11-15 human: created.
2. factory: started doing as heishamon.
3. builder: Built: the control code's constant text moved to flash on the ESP8266. No text, topic, log line or page byte changed, and no upstream file or hook line changed.

   RAM, D1 mini "Variables and constants in RAM": 49108 bytes on develop at this story's start, 46484 bytes now (−2624; the criterion asks for ≥ 2500). ESP32 globals are unchanged at 67616. Final `make check` green, chained with the last edit.

   Paths:
   - `HeishaMon/src/control/ctl_flash.h` (new): `CTL_FLASH`, `CTL_FLASH_TEXT`, `CTL_FLASH_CHAR`, `CTL_FLASH_LENGTH`, `CTL_FLASH_COMPARE`, `CTL_FLASH_COPY`. On the ESP8266 they are `PROGMEM`, `PSTR`, `pgm_read_byte` and the `_P` functions. On the host and the ESP32 they are plain constants and the ordinary functions.
   - `loop/ctl_text.{h,c}`: new `ctl_text_put_flash`, which appends a flash text a byte at a time and refuses as `ctl_text_put` does. The char append is factored into a static `put_char`.
   - `glue/ctl_telemetry.c`: every subtopic text, and `config/`, through `ctl_text_put_flash(CTL_FLASH_TEXT(...))`.
   - `glue/ctl_settings.c`: the fixed-name table is now `fixed_name {char name[24]; setting_id id;}` in flash. Lookup uses `CTL_FLASH_COMPARE` and copies the entry for its id; `copy_fixed_name` serves `ctl_settings_name_at`.
   - `glue/ctl_reset_reason.c`: the two name tables are `char [][20]` in flash, plus the "exception " and "reason " texts.
   - `glue/ctl_html.c`: every snippet through a new `put_flash`. `entity_of` is now a small flash table, because gcc had turned the switch into a 116-byte table of pointers in RAM.
   - `glue/ctl_form.c`: the save-result texts are in flash and copied by `copy_last_save` into a 40-byte buffer.
   - `glue/ctl_dispatch.c`: the `integral_reset` and `energy_wh` comparisons use `CTL_FLASH_COMPARE`.
   - `loop/ctl_loop.c`: the record line's keys are in flash.
   - `glue/ctl_firmware.cpp`: every `log_text` format is `PSTR` and printed with `vsnprintf_P`. `gate_text` became `log_gate`, which logs one `PSTR` line per gate with the same text. Topic formats use `snprintf_P`. The two fixed subtopics go through a new `publish_flash`. The two file-write consequences are `PSTR`, copied into RAM by `copy_flash`.
   - Tests:
     - `tests/control/test_control_page.c` (new) renders the page's dynamic part: status, outputs and inputs rows; the settings open and close text; every setting row in its editable and too-long forms; and the settings' telemetry rows. It uses fixed settings: 3 rooms, escaped topics, and a save with saved, unchanged and both refusal results. It compares byte for byte with `tests/control/data/control_page.html`, which I captured from the unchanged develop code before any code change, and reports the first differing offset.
     - `tests/control/test_flash_text.c` (new) covers `ctl_text_put_flash`: append, refusal, and an empty buffer.
     - `test_telemetry.c` is unchanged and green.
     - `test_traces` replays every golden trace bit for bit.
   - Docs: a "The RAM budget" section at the end of `docs/control/firmware.md` gives the reason and the rule for new text, and `docs/control/decisions.md` has the entry "Constant text in flash" (rejected: leaving it in RAM).

   Decisions where the story left room:
   - The core (`ctl_types.c`, 296 bytes of type names) stays as it is. The core may include only its own headers and four standard ones, and the lint flags any other include there even inside `#ifdef`. The budget does not need it either.
   - Name tables are fixed-width char arrays in flash rather than pointer arrays, so the pointers do not sit in RAM.
   - `.cpp` files use upstream's `PSTR` and `_P` functions directly; the header is for `.c` files.
   - The page reference is a data file beside the traces rather than a C string literal of about 9 KB.
   - I did not rely on `non32xfer=fast` byte-read emulation. Every flash read goes through the `_P` functions.

   Rejected: a `log_text` macro that adds `PSTR` behind the caller's back. The calls carry `PSTR` explicitly instead.

   Smoke test: `make test 2>&1 | grep -E '^control: [0-9]+ test programs passed$' | wc -l` gives `1` (expected `1`). `git diff develop --stat -- HeishaMon ':!HeishaMon/src'` is empty.

   Noticed, not touched:
   - Small texts still in RAM, about 300 bytes: `setting_status_name`'s words, the file paths in `ctl_firmware.cpp`, the router, gate and desired-source names (they come back as pointers that tests compare directly), and `ctl_dispatch`'s `none` struct (44 bytes).
   - ArduinoJson's error-message table (`DeserializationError::c_str`) sits in RAM through upstream's library.

   `check`: check: green
4. factory: started review as heishamon.
5. reviewer: Findings:

   1. The `## Description` of this work item doesn't match what was built. Its "Why" lists "the type names" among the 4.0 KB, and it says "This story moves the control code's constant text to flash". The built code leaves the core's type names in RAM (`HeishaMon/src/control/core/ctl_types.c`, 296 bytes), because the core's include rule allows no flash header there. It also leaves about 300 bytes of small glue texts in RAM, as the journal says. Fix: rewrite the Description so it says the core's type names stay in RAM and why (as `docs/control/decisions.md` and `docs/control/firmware.md` already do). If the human wants them moved after all, end with `question` instead, since that would mean changing the core's include rule.
   2. `tests/control/test_control_page.c:162-163` has a false comment. It says "The rows of the page in the order the firmware sends them", but `glue/ctl_web.cpp` sends a setting's too-long row only instead of its editable row, never both. It also never puts the `config/` telemetry rows on the page. The test interleaves all three for every setting. Lines 20-21 call the reference "The page as the firmware rendered it before its constant text moved to flash". That is wrong for the same reason, and the comment is about history rather than timeless. Fix: say that the test renders every row form the `.c` renderers produce (telemetry groups, both setting-row forms, settings telemetry, and the settings open and close text) and compares it with a captured reference, so a change of where the text lives can't change a byte.

   Checked with no finding:
   - Criterion 1: I ran `make firmware` myself. The D1 mini reads 46484 bytes and the ESP32 67616, as reported. The develop figure of 49108 is taken from the journal; I did not rebuild develop.
   - Criterion 2: `test_control_page.c` covers every snippet moved in `ctl_html.c`, `ctl_form.c` and `ctl_telemetry.c`. `test_telemetry.c` is unchanged.
   - Criterion 3: `test_traces` is untouched, and so is the core.
   - Criterion 4: `git diff develop...HEAD --stat -- HeishaMon ':!HeishaMon/src'` is empty.
   - Criterion 5: the journal reports `check: green`.
   - Criterion 6: both documents are present, and their claims match the code.
   - Every literal moved in the diff keeps its exact text.
   - No flash pointer is read directly. Every one reaches `ctl_text_put_flash`, `put_flash`, `CTL_FLASH_COMPARE`, `CTL_FLASH_COPY`, `copy_flash` or `vsnprintf_P`/`snprintf_P`.
   - Fixed-width tables have room: the longest fixed setting name has 21 characters in a 24-byte slot, the longest reset reason name 18 in 20, and the longest entity 6 in 7.
   - Copy buffers fit their longest texts: the longest save result has 38 characters in `LAST_SAVE_MAX` 40, and the longest consequence 36 in `CONSEQUENCE_SIZE` 48.
   - No dynamic memory, recursion or unbounded loop was added.
   - Earlier tests: none changed. Every test file in the diff is new (`test_control_page.c`, `test_flash_text.c`, `data/control_page.html`).
6. factory: started doing as heishamon.
7. builder: Round 1: I fixed both review findings and changed nothing else.

   1. Description: I added a "What stays in RAM" paragraph to the work item's `## Description`, after the "Why" paragraph. It says the core's type names (`core/ctl_types.c`, about 300 bytes) stay in RAM. The reason is that the core includes only its own headers and four standard ones, so it cannot include the flash header, and the RAM budget does not need those names moved. The paragraph also says a few short glue texts stay in RAM, about 300 bytes: names returned to callers as pointers, file paths and a status word table. This matches what `docs/control/decisions.md` and `docs/control/firmware.md` already say. I did not ask about moving the type names, because that would mean changing the core's include rule and the budget is met without it.
   2. `tests/control/test_control_page.c`: I rewrote two comments.
      - The reference comment now says the file is a captured rendering of every row form the renderers produce, and that where their text lives must not change a byte of it.
      - The `render_page` comment now says it renders every row form so that each snippet is compared: the telemetry groups, the settings' open and close text, each setting in both its editable and its too-long form, and the settings' telemetry.

      Neither comment claims to show the firmware's page order any more, and neither talks about history. The test code and the reference data did not change.

   Results: `make check` passed (`check: green`), chained with the last edit. The D1 mini figures are unchanged from the earlier round, and the ESP32 global RAM still reads 67616 bytes. Smoke test: `make test 2>&1 | grep -E '^control: [0-9]+ test programs passed$' | wc -l` gives `1` (expected `1`).

   Nothing new noticed.

   The factory left out changes outside the write paths: factory/features/F0026-give-ram-back/in-progress/F0026-S0001-constant-text-in-flash.md.

   `check`: check: green
