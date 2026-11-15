# Fewer room slots and shared buffers

## Description

**Why.** After S0001, the control code's largest RAM items are its fixed tables and buffers: the settings hold 16 room slots with a 96-character topic each (about 1.9 KB), and the firmware glue keeps several separate text buffers (log line, topic, subtopic, value, setting value, integral text, and in the web glue the desired-setpoint and form report buffers) that are never used at the same time. The owner uses 5 rooms.

**What changes.** `CTL_ROOMS_MAX` goes from 16 to 8, and every table sized by it shrinks with it. A `/control.json` or a message that sets `rooms` above 8, or a room index of 8 or more, is refused like any out-of-range value; a file with more rooms loads the first 8. The glue's scratch buffers that are never in use at the same time share storage; each use keeps its own size limit, and no text is cut shorter than today.

**What stays.** Every topic, value, setting, log text and page byte for up to 8 rooms; the core's behaviour; the golden traces.

## Interface

- `CTL_ROOMS_MAX` 8, with every dependent size.
- The shared buffers in `glue/ctl_firmware.cpp` and `glue/ctl_web.cpp`.
- The leave for tests another story wrote: tests that use rooms 9 to 16, or that check the room limit or a count derived from it (`tests/control/test_rooms.c`, `tests/control/test_settings*.c`, `tests/control/test_form*.c`, `tests/control/test_telemetry.c`, `tests/control/test_html.c`, `tests/control/test_lean_page.c`), may change their room numbers, limits and counts to the new limit; no other check may be removed or loosened.
- No upstream file changes, and no hook line changes.

## Acceptance criteria

1. `make firmware` builds both boards, and the D1 mini's global RAM is at least 3,500 bytes below 49,108 bytes (the feature's baseline). Report the number.
2. Setting `rooms` to 8 is accepted and to 9 refused; `room.7.topic` is accepted and `room.8.topic` refused; a `/control.json` with 10 rooms loads the first 8 and refuses the rest.
3. The control page and the telemetry for 5 rooms are byte for byte as before.
4. Every golden trace replays bit for bit.
5. `git diff develop --stat -- HeishaMon ':!HeishaMon/src'` shows no upstream file changed.
6. `make check` stays green.
7. docs: `docs/control/settings.md` says the room limit is 8; `docs/control/firmware.md` updates its RAM budget section; `docs/control/decisions.md` gets one entry: 8 room slots and shared buffers, to give the web server RAM; rejected: 16 slots (two thirds of them unused on the owner's board).

## Smoke test

```sh
make test 2>&1 | grep -E '^control: [0-9]+ test programs passed$' | wc -l
```

Expect: `1`

## Journal
