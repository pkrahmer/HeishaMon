---
stage: review
blocked: null
round: 0
attempts: 0
crashes: 0
pull_request: 30
comments_read: []
handed_over: null
claim: null
journal:
- number: 1
  speaker: human
  kind: created
  at: '2026-11-13T17:21:45.452435Z'
  stage: doing
- number: 2
  speaker: factory
  kind: started
  at: '2026-11-13T17:21:45.458649Z'
  stage: doing
- number: 3
  speaker: builder
  kind: result
  at: '2026-11-13T17:37:35.576887Z'
  stage: doing
  run: 8bc4543790fb4f38887d432dabb93017
---

# Descriptions into a document

## Description

**Why.** Since F0015, the control page (`/control`) shows one description per setting. On the owner's production board, an ESP8266 HeishaMon small board, the page is about twice as large as before. Production restarted twice, 3½ minutes apart, while the owner had the page open and the MQTT log ran at about nine publishes a second. The likely cause is RAM: upstream's web server copies every piece a page sends and holds it until the browser has fetched it. The owner decided: the page goes back to the minimum, and the descriptions move into a document.

**The page after this story.** The settings table has three columns: **Setting**, **Value** and the save result of F0015, unchanged. No description is written. Above the settings table, one line links to the document:

```html
<p><a href="https://github.com/pkrahmer/HeishaMon/blob/develop/docs/control/settings.md">What the settings mean</a></p>
```

The status and inputs tables stay as they are.

**The firmware.** Every description text leaves the firmware, together with `ctl_settings_describe` and everything only it served. Nothing about the descriptions stays in flash or RAM.

**The document.** A new file `docs/control/settings.md`, for the owner and for anyone who sets up a board. It holds:

- one short paragraph: what the settings are, that they are set on `/control` or over MQTT (`<base>/control/set/<name>`), and that a refused value keeps the old one;
- one table with a row per setting, in the order of `ctl_settings_name_at`, with the room settings once as `room.<i>.topic` and `room.<i>.weight`. Its columns are **Setting**, **Default**, **Allowed** and **What it does**. The text of **What it does** is the description F0015 wrote into the code, as its builder corrected it against the code, without the unit and range where those now stand in **Allowed**. Every default and every rule is checked against the code; a difference is corrected in the document and listed in the result;
- one section *Running a board on the pump with Smart Grid mode*: the settings the owner's production board uses (`smart_grid` 1, `frame_log_topic` and `command_topic` empty, `enabled` 1), and that Smart Grid value 1 stops heating only on a pump whose Capacity 1 the installer set to a curve that never heats;
- one section *Running a board without a pump*: `frame_log_topic`, `command_topic`, `enabled` 0 or 1, pointing to the sections of `firmware.md` that describe them.

The section *The settings* of `docs/control/firmware.md` keeps its rules and points to `settings.md` for the meaning of each setting; the section *The control page* describes the three columns and the link.

## Interface

- Removed: `ctl_settings_describe` and its tests. The leave for tests another story wrote: `tests/control/test_descriptions.c` may be deleted; `tests/control/test_form_rows.c`, `tests/control/test_form.c` and `tests/control/test_html.c` may change only where they test the description column. No other test changes.
- No upstream file changes, and no hook line changes.

## Acceptance criteria

1. A host test renders the settings rows with `rooms` 4 and checks that no row contains a description: none of the texts `Send commands to the pump`, `MQTT topic of room` and `The house's heat loss` appears, and every row has exactly three cells.
2. A host test checks that the page's settings part contains the link of the Description exactly once.
3. `git grep -n "ctl_settings_describe" -- HeishaMon tests` prints nothing.
4. `docs/control/settings.md` has one table row per setting that `ctl_settings_name_at` lists with `rooms` 1 (the room settings as `room.<i>.…`), and each row has a default, an allowed range and a description.
5. `make firmware` compiles both boards, and its memory lines for the D1 mini show less flash and no more global RAM than on `develop`.
6. `git diff develop --stat -- HeishaMon ':!HeishaMon/src'` shows no upstream file changed.
7. `make check` stays green.
8. docs: as the Description says, and `docs/control/decisions.md` gets one entry: the descriptions moved from the page into `settings.md`, because the page's size cost RAM on the ESP8266.

## Smoke test

```sh
make firmware | grep -c -E '^(esp8266|esp32):'
```

Expect: `2`

## Journal

1. 2026-11-13 human: created.
2. factory: started doing as heishamon.
3. builder: Built: the descriptions are gone from the page and the firmware, and they now live in `docs/control/settings.md`. `make check` is green.

   Code:
   - `HeishaMon/src/control/glue/ctl_settings.{c,h}`: removed `ctl_settings_describe`, `CTL_SETTINGS_DESCRIPTION_MAX`, the `DESCRIPTIONS` table and everything only it used (`IN_FLASH`, `<sys/pgmspace.h>`, `flash_char`, `ROOM_NUMBER_MARK`, `append_char`, the static size assert).
   - `glue/ctl_html.{c,h}`: `ctl_html_setting` has no `description` field, so a settings row has three cells. New macros `CTL_HTML_SETTINGS_LINK`, `CTL_HTML_SETTINGS_OPEN` (the h2, the link line, the form and the table head Setting/Value/Save result) and `CTL_HTML_SETTINGS_CLOSE`.
   - `glue/ctl_form.{c,h}`: `ctl_form_row` no longer asks for a description.
   - `glue/ctl_web.cpp`: `FORM_OPEN` and `FORM_CLOSE` are now `FLASHPROG` copies of those macros. No upstream file and no hook line changed.

   Tests:
   - New `tests/control/test_lean_page.c` (criteria 1 and 2): with rooms 4, every row has exactly three `<td>` cells and none of the three named texts. The settings part (open + rows + close) holds the link exactly once, above the table, and the table head has three columns.
   - New `tests/control/test_settings_doc.c` (criterion 4): with rooms 1, each name from `ctl_settings_name_at` (room names as `room.<i>.…`) has exactly one row in `settings.md`, and each row has four non-empty cells. I checked by hand that it fails when a row is removed or a default is blanked, then restored the file.
   - Deleted `tests/control/test_descriptions.c` (it shows as staged, from `git rm`).
   - In `test_form_rows.c` and `test_html.c` I changed only the expected strings and the struct initialisers that held the description column.

   Docs:
   - New `docs/control/settings.md`: an intro paragraph, the table (Setting/Default/Allowed/What it does, in list order), *Running a board on the pump with Smart Grid mode* and *Running a board without a pump*.
   - `firmware.md`: *The settings* now points to `settings.md` (and says "the control page" instead of the stale "a later web page"); *The control page* describes three columns and the link, and the paragraphs on module coverage and memory no longer mention the descriptions.
   - `decisions.md`: one new entry.

   Defaults and rules checked against the code:
   - Every default (core, rooms, router) and every rule matched.
   - Corrections in the document: `room.<i>.weight` now says that 0 means watched but not counted, and that all weights together must be above 0 (a set that breaks this is refused as invalid; the old description left both out). Defaults that firmware.md calls "the core's default" are written as numbers (22, 155, 805, 1500, 0.02).
   - Wording change: the `rooms` text "Save first" now says "On the control page, save the count first", since the document is also for MQTT users.
   - No other differences.

   Decisions:
   - The page texts are header macros rather than a `.c` function. The test reads the same string the page sends, and the `.cpp` keeps it in flash. A string constant in a `.c` file would sit in RAM on the ESP8266.
   - The link sits between the h2 and the form.
   - Rejected: leaving the texts only in `ctl_web.cpp`, where no host test can reach them.

   Criterion 5, D1 mini memory, develop → branch:
   - code flash 761528 → 757288 B (−4240)
   - global RAM 50628 → 50628 B (no change)
   - ESP32: sketch 1709679 → 1705551 B, globals unchanged

   Criterion 3: `git grep -n ctl_settings_describe -- HeishaMon tests` prints nothing. Criterion 6: the diff stat outside `HeishaMon/src` is empty.

   Smoke test: `make firmware | grep -c -E '^(esp8266|esp32):'` printed `2` (expected 2).

   Noticed, not touched: the table in firmware.md's *The settings* still says "as the core validates it" for several rules; I left it, since the story keeps that section's rules.

   `check`: check: green
