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
  started_at: '2026-11-12T23:57:49.741149Z'
  lease_duration: 60
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
