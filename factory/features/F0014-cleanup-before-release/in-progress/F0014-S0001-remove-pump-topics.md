---
stage: review
blocked: null
round: 0
attempts: 0
crashes: 0
pull_request: 28
comments_read: []
handed_over: null
claim:
  instance: heishamon
  stage: review
  started_at: '2026-11-13T12:06:02.081958Z'
  lease_duration: 60
journal:
- number: 1
  speaker: human
  kind: created
  at: '2026-11-13T12:22:33.107404Z'
  stage: doing
- number: 2
  speaker: factory
  kind: started
  at: '2026-11-13T12:22:33.113228Z'
  stage: doing
- number: 3
  speaker: builder
  kind: result
  at: '2026-11-13T11:35:43.703636Z'
  stage: doing
  run: 3673bee1fac64d0a9135694c03802e97
- number: 4
  speaker: factory
  kind: started
  at: '2026-11-13T12:06:02.081958Z'
  stage: review
---

# Remove the pump topics and dead code

## Description

**Why.** The fork goes onto the owner's production HeishaMon next. The four pump-value topics of F0009 were a bridge for a board without a pump: `outdoor_topic`, `heat_topic`, `valve_topic` and `defrost_topic`. While any of them is set, the frame hook hands nothing to the loop. Since F0011 a board without a pump reads real frames from another board's log, and a board on a pump reads its own frames. Nothing needs the topics now, and a leftover value in a `control.json` would cut the loop off from the pump. This story removes them, and removes code in the fork's own folders that nothing calls or reaches.

**The pump topics.** After this story:
- the four settings do not exist: not in the name list, not on the control page, not in the `config` telemetry, not in `/control.json` when it is written;
- a `/control.json` that still holds any of them loads as if they were absent, like any unknown name today, and the other settings in it keep their values;
- the dispatch has no pump-topic kinds, and the glue subscribes to no pump topic;
- the frame hook always hands the pump values from frames to the loop, from the board's own pump or from the frame log;
- `ctl_settings_pump_from_mqtt` and everything only it served are gone.

**Dead code.** In `HeishaMon/src/control/`, `tests/control/` (except `tests/control/device/`) and `docs/control/`, the builder removes:
- functions, types, macros and constants that nothing uses after the removal above;
- branches that cannot be reached;
- comments and doc lines about removed code.

The builder lists each removal outside the pump topics in its result, with one line on why nothing reaches it. Something a header documents as part of a module's contract and that a test uses is not dead, even if the firmware does not call it. When in doubt, the builder keeps it and lists it as a doubt.

**What stays.** The thermostat path, the gate, the frame log, `command_topic`, `smart_grid`, the telemetry of every remaining setting and input, the control page for every remaining setting, and everything under `tests/control/device/`.

## Interface

- Settings: the four names leave the name list. The name count with `rooms` 0 becomes 19.
- The leaves for tests another story wrote: the builder may change `tests/control/test_settings.c`, `tests/control/test_dispatch.c`, `tests/control/test_telemetry.c`, `tests/control/test_form.c` and `tests/control/test_html.c` only to remove rows about the four settings and to update counts and positions that the removal shifts. It may delete a whole test file only when every check in it is about removed code. No other test changes.
- New test: `tests/control/test_settings_legacy.c` loads a `control.json` text holding `"heat_topic":"a/b"` and `"outdoor_topic":"c/d"` together with `"hysteresis_wh":1200`, and checks that loading succeeds, `hysteresis_wh` is 1200, and a written settings text holds neither removed name.
- No upstream file changes, and no hook line changes.

## Acceptance criteria

1. `git grep -nE "outdoor_topic|heat_topic|valve_topic|defrost_topic|pump_from_mqtt" -- HeishaMon/src/control tests/control` prints nothing, except lines of `tests/control/test_settings_legacy.c`.
2. `tests/control/test_settings_legacy.c` passes as the Interface says.
3. The name count is 19.
4. The result lists every removal outside the pump topics, and every doubt kept.
5. `make firmware` compiles both boards, and `git diff develop --stat -- HeishaMon ':!HeishaMon/src'` shows no upstream file changed.
6. `make check` stays green.
7. docs: `docs/control/firmware.md` no longer describes the four settings or a board that takes pump values from MQTT. Its section *Shadowing another HeishaMon* now points to *Frames from another HeishaMon's log* as the way to feed a board without a pump. `docs/control/decisions.md` gets one entry: the pump topics removed, why, and that the frame log replaces them; earlier entries stay as they are.

## Smoke test

```sh
make firmware | grep -c -E '^(esp8266|esp32):'
```

Expect: `2`

## Journal

1. 2026-11-13 human: created.
2. factory: started doing as heishamon.
3. builder: Removed the four pump topics and what only they served. Loading a `control.json` is now done by a pure reader the host tests run, so the required legacy test checks the code the board actually uses. `make check` is green (lint, test, device, firmware). The smoke test `make firmware | grep -c -E '^(esp8266|esp32):'` printed `2`, as expected.

   What I built:
   - **Pump topics removed:**
     - `HeishaMon/src/control/glue/ctl_settings.{c,h}`: the four setting ids, name rows, struct fields, defaults and topic lookups are gone, as is `ctl_settings_pump_from_mqtt`. `CTL_SETTINGS_FIXED_NAMES` drops from 23 to 19, so the name count with `rooms` 0 is 19.
     - `HeishaMon/src/control/glue/ctl_dispatch.{c,h}`: the table `is_named_input` is gone. Only `pv_topic` is still matched as a named input, by a direct `strcmp`. The header comment is updated.
     - `HeishaMon/src/control/glue/ctl_firmware.{cpp,h}`: the glue no longer subscribes to the four topics. `ctl_firmware_frame` always hands the frame values to the loop. `log_pump_source` and its two statics are removed. Header comments no longer mention pump topics.
   - **New pure module** `HeishaMon/src/control/glue/ctl_settings_file.{c,h}`:
     - A reader you feed one character at a time. It sends each text pair to `ctl_settings_set` and skips pairs with any other value. A name the setter does not list is refused like any unknown name, so an older file still loads.
     - A writer that produces the file in pieces.
     - `ctl_firmware.cpp` now uses both for `/control.json` and no longer uses ArduinoJson.
   - **Tests:**
     - `tests/control/test_settings_legacy.c` follows the Interface.
     - `tests/control/test_settings_file.c` covers the new module: events, escapes, values that are not text, broken or too-long strings, malformed objects, the pieces, and a full round trip with a quote and a backslash in a topic.
     - `tests/control/test_settings.c`: removed the four default rows, `test_pump_topics`, `test_pump_from_mqtt` and the `pump_from_mqtt` check inside `test_frame_log_topic`. Counts changed from 23/27/25 to 19/23/21, and positions from 24/26/27 to 20/22/23.
     - `tests/control/test_dispatch.c`: removed `set_pump_topics` and `test_pump_topics`, whose rows are all about the four topics, and the `defrost_topic` row of `test_frame_log_topic`.
   - **Docs:**
     - `docs/control/firmware.md`: the four table rows are gone. *Shadowing another HeishaMon* now points to *Frames from another HeishaMon's log*, and step 4 sets `frame_log_topic`. The "four pump topics must be empty" paragraph and the pump-topic mention under setting changes are removed. *The settings file* names the reader and writer, and says how an unknown legacy name is treated.
     - `docs/control/decisions.md`: one new entry at the end. Earlier entries are unchanged.

   Decisions and what I rejected:
   - **Why a new reader:** the Interface asks a host test to "load a control.json text", and no host-side loader existed. ArduinoJson only runs in the firmware. I rejected:
     - a test helper that parses JSON the way the glue does, because it would test a copy of the glue rather than the glue;
     - reading the whole file into a buffer, up to about 7 KB with 16 rooms, too much for the ESP8266 stack and wasteful as a static.

     The reader keeps the old behaviour: values that are not text are logged and skipped, refusals are logged and skipped, and a file that is not an object, or is cut short, gives the defaults. The decision entry records this.
   - **A second new test file:** I wrote `test_settings_file.c` because the new module needs tests of its own. "No other test changes" in the Interface concerns existing tests, so I read it as allowing this.
   - **Loading a legacy file:** a file that names a removed topic loads; the pair is refused as `UNKNOWN`, logged and skipped, and the next write leaves it out. I rejected refusing the whole file, which would reset a production board to its defaults.

   Removals outside the pump topics, with why nothing reaches them:
   - `log_pump_source`, `pump_source_logged`, `pump_from_mqtt` in `ctl_firmware.cpp`: they only logged the MQTT-or-frames source, which no longer exists.
   - `put_text`, `put_json_string` and the ArduinoJson parsing in `ctl_firmware.cpp`: replaced by the new module, so nothing calls them.
   - `is_named_input` in `ctl_dispatch.c`: with only `pv_topic` left, the table had one row.

   Doubts kept:
   - The `default:` branches of the switches in `ctl_settings.c` (`apply_value`, `write_value`, `topic_field`, `number_field`, `number_in_rule`) are unreachable for valid ids. They predate this story and act as guards that keep each switch complete, so I kept them.
   - `CTL_INPUT_OUTDOOR`, `CTL_INPUT_HEAT`, `CTL_INPUT_VALVE` and `CTL_INPUT_DEFROST` stay: the frame hook and `ctl_shadow.c` still use them.
   - A search for public `ctl_`/`CTL_` names found none used only by their own declaration and definition.

   Noticed but not touched:
   - `#include <stdbool.h>` in `test_dispatch.c` is now unused. The Interface allows only row and count edits there, so I left it.
   - Older entries in `decisions.md` still describe the topics, as the criterion requires.
   - `moves_topics` in the glue also treats `command_topic` as a topic that moves subscriptions; that was already so and is unchanged.

   Skill set aside: none was loaded. One `git add -N` I tried, to make new files visible to `git grep`, was denied; I used `git grep --untracked` instead. Criterion 1's grep finds only lines in `test_settings_legacy.c`, and criterion 5's `git diff develop --stat -- HeishaMon ':!HeishaMon/src'` prints nothing.

   `check`: check: green
4. factory: started review as heishamon.
