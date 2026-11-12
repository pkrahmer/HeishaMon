---
stage: doing
blocked: null
round: 1
attempts: 0
crashes: 0
pull_request: 23
comments_read: []
handed_over: null
claim: null
journal:
- number: 1
  speaker: human
  kind: created
  at: '2026-11-12T08:06:29.448826Z'
  stage: doing
- number: 2
  speaker: factory
  kind: started
  at: '2026-11-12T08:06:29.455689Z'
  stage: doing
- number: 3
  speaker: builder
  kind: result
  at: '2026-11-12T08:16:01.899234Z'
  stage: doing
  run: 204fcf85989c4af289f310c35185c882
- number: 4
  speaker: factory
  kind: started
  at: '2026-11-12T08:16:06.743355Z'
  stage: review
- number: 5
  speaker: reviewer
  kind: result
  at: '2026-11-12T08:17:05.938591Z'
  stage: review
  run: 18bc86d353874978a0ba8c4129f52e6f
---

# The pump values from MQTT

## Description

A board without a pump never decodes a frame, so its loop never gets the outdoor temperature, the produced heat, the valve or the defrost flag. This story adds four settings that name MQTT topics for these four values, as another HeishaMon publishes them, for example `jeisha/main/Outside_Temp`. When a topic is set, the glue subscribes to it and hands its messages to the loop as the matching input. While any of the four is set, the frame hook hands no pump value to the loop. A board on a pump leaves them empty and behaves exactly as before.

## Interface

Changed files: `HeishaMon/src/control/glue/ctl_settings.h`, `ctl_settings.c`, `ctl_dispatch.c`, `ctl_firmware.cpp`, and their tests `tests/control/test_settings.c` and `tests/control/test_dispatch.c`, which change by the owner's leave as the criteria say. `docs/control/firmware.md` changes by the docs criterion.

- Four new settings, after `pv_topic` in the order of `ctl_settings_name_at`: `outdoor_topic`, `heat_topic`, `valve_topic` and `defrost_topic`. Each takes a topic by the same rule as `pv_topic`, empty by default.
- `ctl_settings_name_count` with `rooms` at 0 becomes 20.
- `ctl_dispatch_topic` gives `INPUT` with `CTL_INPUT_OUTDOOR`, `CTL_INPUT_HEAT`, `CTL_INPUT_VALVE` or `CTL_INPUT_DEFROST` for a topic equal to the matching non-empty setting. Room topics are checked first, then the PV topic, then these four in the order named.
- `bool ctl_settings_pump_from_mqtt(const ctl_settings *settings)` is true when any of the four is non-empty.
- **The glue:**
  - `ctl_firmware_subscribe` also subscribes to every non-empty pump topic;
  - `ctl_firmware_frame` hands no value to the loop while `ctl_settings_pump_from_mqtt` is true;
  - the glue logs once, at setup and on each change, which source the pump values come from.

## Acceptance criteria

1. The four settings, as rows of one table test: each accepts `jeisha/main/Outside_Temp` and the empty text, and refuses `a+b` with `BAD_VALUE`; their defaults are empty.
2. The name list: with `rooms` 0 the count is 20, and the four names follow `pv_topic` in the order named. The earlier rows of `test_settings.c` that state the count or the order change by the owner's leave to match, and no other row changes.
3. `ctl_settings_pump_from_mqtt` is false for the defaults, and true when any one of the four is set.
4. `ctl_dispatch_topic`, as rows of one table test:
   - with all four set to the `jeisha/main/` topics, each gives its input;
   - with them empty, `jeisha/main/Outside_Temp` gives `NONE`;
   - a topic equal to a room topic and to `outdoor_topic` gives the room.
5. `make firmware` compiles both boards.
6. `make check` stays green.
7. docs: `docs/control/firmware.md` lists the four settings in *The settings* and gets a section *Shadowing another HeishaMon*. That section covers:
   - the four topics;
   - that the frames then hand nothing;
   - the steps to run the firmware on a spare board beside a production HeishaMon: its own MQTT base, listen-only, commands left disabled, the four `jeisha/main/` topics and the room topics.

## Smoke test

```sh
make firmware | grep -c -E '^(esp8266|esp32):'
```

Expect: `2`

## Journal

1. 2026-11-12 human: created.
2. factory: started doing as heishamon.
3. builder: Built the four pump topic settings and the MQTT path for the pump's values.

   Code:
   - `HeishaMon/src/control/glue/ctl_settings.h`, `ctl_settings.c`: added `outdoor_topic`, `heat_topic`, `valve_topic` and `defrost_topic` after `pv_topic`. They use the same rule as `pv_topic` and are empty by default. `CTL_SETTINGS_FIXED_NAMES` is now 20. Added `ctl_settings_pump_from_mqtt`. The setter and getter now find every topic setting (the room topics, `pv_topic` and the four new ones) through one helper, `topic_field`, instead of one switch case per topic.
   - `HeishaMon/src/control/glue/ctl_dispatch.c`, `.h`: rooms are still checked first. After them comes one table, in the order PV, outdoor, heat, valve, defrost, that gives the matching `CTL_INPUT_*`. It replaces `is_pv`. The header comment now describes the new order.
   - `HeishaMon/src/control/glue/ctl_firmware.cpp`:
     - `subscribe_inputs` (and so unsubscribe too) also handles every non-empty pump topic.
     - `ctl_firmware_frame` returns early while `ctl_settings_pump_from_mqtt` is true.
     - `log_pump_source` logs the source at setup and at the end of every change, but only when the source differs from the one logged last.
     - `moves_topics` is now `rooms`, or any name ending in `.topic` or `_topic`, so changing a pump topic over MQTT moves the subscriptions. The control page already resubscribes on every save.

   Tests:
   - `tests/control/test_settings.c`:
     - New `test_pump_topics`, a table over the four names: each defaults to empty, takes `jeisha/main/Outside_Temp` and the empty text, and refuses `a+b` with `BAD_VALUE`.
     - New `test_pump_from_mqtt`: false at the defaults, true when any one of the four is set.
     - With the owner's leave, only the rows that state the count or order changed. `test_defaults` now expects a count of 20 and has the four names after `pv_topic`. In `test_name_list` the count is 24 and the indices are 23 and 24. In `test_rooms` the count is 22.
   - `tests/control/test_dispatch.c`: new table test `test_pump_topics`. With the four `jeisha/main/` topics set, each topic gives its input, and a room topic and the PV topic still give theirs. With the four empty, `jeisha/main/Outside_Temp` and `jeisha/main/Defrosting_State` give NONE. A topic equal to `room.1.topic` and to `outdoor_topic` gives room 1.
   - I saw the new tests fail before writing the code.

   Docs:
   - `docs/control/firmware.md`: the four rows in *The settings*. The MQTT change paragraph now names the pump topics. New section *Shadowing another HeishaMon*: the topic table, that the frames then hand nothing, the log lines, and the steps for a spare board (its own MQTT base, listen-only, `enabled` left at 0, the four `jeisha/main/` topics, the room topics and PV).
   - `docs/control/decisions.md`: a new entry for the decisions below.

   Decisions:
   - One switch for all four: while any topic is set, the frames hand nothing, even the values whose topic is empty. The story's wording asks for this, and it never mixes two pumps. I rejected switching each value separately and adding a separate on/off setting.
   - A topic two non-room settings share goes to the first in the order PV, outdoor, heat, valve, defrost.
   - The source is logged only when it changes, not on every frame skipped.

   Checks:
   - `make check`: green (lint, test, device, firmware; `check: green`).
   - Smoke test `make firmware | grep -c -E '^(esp8266|esp32):'` gave `2`, as expected.

   Noticed but not touched: upstream's rules harness prints compiler warnings (`rules.cpp`, `settimer.cpp`) during `make test`. They are upstream's, and the build stays green.

   `check`: check: green
4. factory: started review as heishamon.
5. reviewer: Review, round 0. I read the diff against develop, plus `ctl_firmware.cpp` around `subscribe_inputs`, `ctl_firmware_change_end`, `ctl_firmware_frame` and `ctl_firmware_mqtt`, and `ctl_web.cpp`'s call to `ctl_firmware_change_begin`. The factory's `make check` after the builder was green, as the journal says.

   Findings:
   1. `tests/control/test_dispatch.c` `test_pump_topics`: the Interface fixes the order rooms, then PV, then outdoor, heat, valve and defrost. Only rooms-before-outdoor is tested. No row shows that PV comes before a pump topic, or that the four keep their order among themselves, so a table in another order would still pass. Fix: add a row where `pv_topic` equals `outdoor_topic` (expects `CTL_INPUT_PV`) and a row where two of the four share a topic, for example valve and defrost (expects `CTL_INPUT_VALVE`).
   2. `tests/control/test_settings.c` `test_pump_from_mqtt`: the failing side is tested only at the defaults. An implementation that also counted `pv_topic` (or a room topic) would pass. Fix: add a check that `pv_topic` set with the four empty gives false.
   3. Gap in the story, for the human (`ctl_firmware.cpp:293`, `:300`, `:497`): three glue rules have no criterion beyond criterion 5 (it compiles). They are: frames hand nothing while `ctl_settings_pump_from_mqtt` is true, the pump topics are subscribed, and the source is logged. Fix: the human either accepts compile-only coverage for these `.cpp` lines, or adds a criterion over a pure helper, for example one that lists the input topics to subscribe.
   4. `docs/control/firmware.md`, section *Shadowing another HeishaMon*, has two claims that do not match the code:
      - "through the same adapters as a decoded frame's values (`ctl_dispatch_topic`)": `ctl_dispatch_topic` only matches the topic. The value reaches the loop through `ctl_loop_input` (`ctl_firmware.cpp:530`).
      - "logs … at setup and on each change": `log_pump_source` logs only when the source differs from the one logged last.
      Fix: name `ctl_loop_input` there, and say the source is logged at setup and whenever it changes.
   5. `HeishaMon/src/control/glue/ctl_settings.h`, the comment on `ctl_settings_defaults`: the edited line is 113 characters, while the file wraps at 100. Fix: rewrap the comment.

   Earlier tests:
   - `tests/control/test_settings.c` (M): the story names it with the owner's leave. Only rows that state the count or the order changed:
     - `test_defaults`: count 20, plus the four name rows after `pv_topic`.
     - `test_name_list`: count 24 and indices 23 and 24.
     - `test_rooms`: count 22.
     The rest of the changes only add tests.
   - `tests/control/test_dispatch.c` (M): the story names it. The change only adds `test_pump_topics` and its call; every earlier test is as it was.

   Checked and found fine:
   - Architecture: no file outside the fork's three folders changed, and the `.c` files stay pure.
   - Embedded safety: the loops are bounded by fixed tables, the four topic buffers are fixed size, and nothing allocates.
   - The `.topic`/`_topic` rule in `moves_topics`.
   - The decisions entry, and the Description against the build.
