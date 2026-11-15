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
  started_at: '2026-11-15T21:58:13.376921Z'
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
