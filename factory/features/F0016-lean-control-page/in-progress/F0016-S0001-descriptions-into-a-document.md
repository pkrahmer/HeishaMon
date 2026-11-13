---
stage: doing
blocked: null
round: 0
attempts: 0
crashes: 0
pull_request: null
comments_read: []
handed_over: null
claim:
  instance: heishamon
  stage: doing
  started_at: '2026-11-13T17:21:45.458649Z'
  lease_duration: 60
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
