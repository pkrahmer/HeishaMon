---
stage: done
blocked: null
round: 0
attempts: 0
crashes: 0
pull_request: 26
comments_read: []
handed_over: approval
claim: null
journal:
- number: 1
  speaker: human
  kind: created
  at: '2026-11-12T23:57:49.735070Z'
  stage: doing
- number: 2
  speaker: factory
  kind: started
  at: '2026-11-12T23:57:49.741149Z'
  stage: doing
- number: 3
  speaker: builder
  kind: result
  at: '2026-11-13T00:05:24.703284Z'
  stage: doing
  run: 63c0d3b53c1640c1b6aa637f76bc32db
- number: 4
  speaker: factory
  kind: started
  at: '2026-11-13T00:05:29.757390Z'
  stage: review
- number: 5
  speaker: reviewer
  kind: result
  at: '2026-11-13T00:06:15.072147Z'
  stage: review
  run: 008e912f3b0e4a5cbb8ab8b0df0fc07b
- number: 6
  speaker: factory
  kind: done
  at: '2026-11-13T00:07:04.100833Z'
  stage: approval
---

# The payload first

## Description

PubSubClient has one buffer for what it receives and what it sends. A message's topic and payload pointers point into that buffer, and anything the glue sends while handling the message overwrites them: a publish, a subscribe, an unsubscribe, and the log, which upstream publishes over MQTT when "log to MQTT" is on.

`handle_setting` in `HeishaMon/src/control/glue/ctl_firmware.cpp` calls `ctl_firmware_change_begin` before it copies the payload. For a setting that moves topics (`frame_log_topic`, a room topic, a pump topic), that call unsubscribes, and the payload is gone before it is read. On the owner's dev board, `jeisha/log` was stored as `/1/ACTUAL_`, ten bytes of a room topic.

This story moves the copy to the first statement of `handle_setting`, with the refusal of a too-long or zero-holding payload unchanged. It then checks every path of `ctl_firmware_mqtt` for the same fault:

- the input path hands the payload to the loop, which reads it before anything can send;
- the frame log path hands it to the parser before anything is decoded or published;
- `handle_setting` copies first;
- no path reads `topic` after anything that can send.

Where a path does not hold, the story makes it hold the same way: copy first.

## Interface

Changed file: `HeishaMon/src/control/glue/ctl_firmware.cpp`. `docs/control/firmware.md` changes by the docs criterion.

- `handle_setting` copies the payload into its local buffer, or refuses it, before any other call.
- A comment at the top of `ctl_firmware_mqtt` states the rule: the topic and the payload live in the client's buffer, and nothing that can send may run before they are read or copied.

## Acceptance criteria

1. In `handle_setting`, the copy, or the refusal, comes before `ctl_firmware_change_begin` and before any call that can publish, subscribe, unsubscribe or log. The builder's entry walks every path of `ctl_firmware_mqtt` and names, for each, the first call that can send and the last read of the topic or payload before it.
2. `make firmware` compiles both boards.
3. `make check` stays green.
4. docs: `docs/control/firmware.md`, in *Changing a setting over MQTT*, states the rule of the client's single buffer in one sentence. `docs/control/decisions.md` records the fault, how it showed on the dev board, and the fix.

## Smoke test

```sh
make firmware | grep -c -E '^(esp8266|esp32):'
```

Expect: `2`

## Journal

1. 2026-11-12 human: created.
2. factory: started doing as heishamon.
3. builder: Built:
   - `HeishaMon/src/control/glue/ctl_firmware.cpp`: `handle_setting` now copies the payload into its local `value`, or marks it as refused (`fits` is false when it is too long or holds a zero byte), as its first statement. Only then does it call `ctl_firmware_change_begin`. The refusal log and what it publishes are unchanged and still run after the begin. A comment at the top of `ctl_firmware_mqtt` states the rule: topic and payload live in the client's single buffer, so nothing that can send may run before they are read or copied.
   - `tests/control/test_payload_first.c` (new): a host test that reads `ctl_firmware.cpp` from the root of the checkout with its comments stripped. It finds the bodies of `handle_setting` and `handle_frame_log` and fails when a call that can send comes before the handler's last whole-word read of `payload`. The calls it counts are change_begin/change/change_end, publish, publish_setting, subscribe_inputs, log_text, log_message, decode_heatpump_data, ctl_firmware_frame and save_settings. Before the fix it failed on `handle_setting` (change_begin before the copy); after the fix it passes.
   - `docs/control/firmware.md`: in *Changing a setting over MQTT*, one sentence on the client's single buffer.
   - `docs/control/decisions.md`: a new entry, "The payload first", with the fault, the dev board symptom (`jeisha/log` stored as `/1/ACTUAL_`), the fix and the rejected options.

   Walk of every path of `ctl_firmware_mqtt` (criterion 1):
   - Topic: its only read is `ctl_dispatch_topic(&settings, topic_base, topic)` at the top, which is pure. It copies the setting's name into `target.name`, a char array in `ctl_topic`. No path reads `topic` after that.
   - Input path: the last read of the payload is `ctl_loop_input(...)`, which is pure loop C. Nothing sends in this path, and `now_s()` only reads `millis()`.
   - Frame log path: the last read of the payload is `ctl_framelog_line(&frame_log, payload, length)`, which copies the bytes into `frame_log.frame`. The first call that can send is `decode_heatpump_data` (upstream publishes); `ctl_firmware_frame` then reads only the parser's copy. This path already held.
   - Setting path: the last read of the payload is the `memchr`/`memcpy` at the top of `handle_setting`. The first call that can send is `ctl_firmware_change_begin` (unsubscribes for a setting that moves topics). The others that can send come later: `log_text` on a refusal, then `ctl_firmware_change` (logs), `ctl_firmware_change_end` (subscribes, logs) and `publish_setting`. This was the one path that did not hold, and it is fixed.

   Decisions and rejected options:
   - A test that reads the source text, because the `.cpp` compiles only in `make firmware` and no host test can call it.
   - Rejected: copying the whole message at the top of `ctl_firmware_mqtt`, which needs a second buffer of the client's size for every message.
   - Rejected: a pure `.c` copy helper, which a test could call but which would not pin the order of the calls in the `.cpp`.
   - I kept the refusal log after the begin so that what a refused message produces stays the same.

   Checks: `make check` is green (lint, test, device, firmware for both boards). Smoke test `make firmware | grep -c -E '^(esp8266|esp32):'` printed `2`, as expected.

   Noticed, not touched: the source-order test matches by name, so a new call that can send would need adding to its `senders` list.

   `check`: check: green
4. factory: started review as heishamon.
5. reviewer: no findings

   What I checked (round 0), in checklist order:

   1. Acceptance criteria
      - Criterion 1: in `HeishaMon/src/control/glue/ctl_firmware.cpp:384-403`, `handle_setting` now runs the `fits` test, the `memchr` and the `memcpy` before `ctl_firmware_change_begin`. The refusal decision is taken before the begin. The refusal log still runs after it, and it reads only `name`.
      - I walked the paths of `ctl_firmware_mqtt` (`:550-574`) myself and they match the builder's walk:
        - Topic: it is read only by `ctl_dispatch_topic`. That function returns `ctl_topic`, whose `name` is a char array (`ctl_dispatch.h:27`), so no pointer into the buffer survives the call.
        - Input path: `ctl_loop_input` reads the payload. The only other call on that path is `now_s()`, which sends nothing.
        - Frame log path: `ctl_framelog_line` copies the payload into `frame_log.frame` before `decode_heatpump_data` runs, and `ctl_firmware_frame` reads only that copy.
      - Test: `tests/control/test_payload_first.c` holds the order in the source. The failing side holds: before the fix, `ctl_firmware_change_begin` came before the `memchr(payload…)`, so the test would fail.
        - Whole-word matching keeps `ctl_firmware_change` apart from `_begin`/`_end`, and `publish` apart from `publish_setting`.
        - The comment stripping is safe: the file has no string literal that holds `//` or `/*`.
        - The file is 20717 bytes, well under the 131072-byte limit, and the test checks that limit.
      - The comment rule from the Interface is present at the top of `ctl_firmware_mqtt`.
      - The journal reports `make check` green after the builder.
   2. Architecture: the diff changes only files in the fork's three folders. It touches no upstream file, no `.ino` or `htmlcode.h`, and adds no include.
   3. Embedded safety: the board code changes only the order of calls. `memcpy` stays bounded by `length < sizeof value`. The large stack buffer is in a host-only test.
   4. Quality: names are clear and there is no dead code. The test reuses `check.h`/`check_true` rather than copying a helper.
   5. Work item hygiene: the journal names the main decision and what was rejected, and the Description matches what was built. The sentence in `docs/control/firmware.md` is accurate. The new `docs/control/decisions.md` entry has the date, decision, reason and rejected options, the dev board symptom (`/1/ACTUAL_`) and the fix, in the same format as the entries before it.
   6. Earlier tests: none changed. In `git diff --name-status develop...HEAD`, the only test file is `tests/control/test_payload_first.c`, with status `A`.
6. factory: done (pull request merged); cost: 2 runs, 5.9 min, 17 turns, 58711 tokens in (391304 more from cache), 11953 out, $0.61
