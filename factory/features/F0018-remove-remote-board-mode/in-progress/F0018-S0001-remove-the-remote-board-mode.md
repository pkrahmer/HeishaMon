# Remove the remote-board mode

## Description

**Why.** The remote-board mode (F0011 frames from another board's log, F0013 commands to another board over MQTT) was a bridge for testing on a board without a pump. The owner's production board now runs the loop itself on its own serial port with both topics empty, and the owner decided to remove the mode so that the firmware carries only what it uses.

**What goes.** The settings `frame_log_topic` and `command_topic`; the frame-log parser `glue/ctl_framelog.c` and `.h`; in the firmware glue, the subscription to the log, the handling of its lines, the reading of sent optional PCB frames and the publishing of commands to `<command_topic>/<name>`; the confirmation result that only the log gave (`CTL_FRAMELOG_SENT` and what serves only it); the gate's reasons and branches for a board that commands another one (including `no_frame_log`); every telemetry value, page row and doc section that exists only for the mode. Anything else that nothing reaches once the mode is gone also goes; each removal is listed in the result.

**What stays.** The board's own path, exactly as it runs on production: frames from its own serial port, commands to its own serial port, Smart Grid mode, the thermostat path, the router, the gate's other reasons, the integral term and its file. With both topics empty today, the board must behave after this story exactly as before.

## Interface

- Removed: the two settings, `glue/ctl_framelog.*`, the remote branches in `glue/ctl_firmware.cpp`, `glue/ctl_gate.*` and `glue/ctl_dispatch.*`, and their telemetry.
- The leave for tests another story wrote: `tests/control/test_framelog.c` and `tests/control/test_framelog_sent.c` may be deleted, and so may any other test that tests only removed code. In `tests/control/test_gate.c`, `tests/control/test_settings*.c`, `tests/control/test_telemetry*.c`, `tests/control/test_form*.c`, `tests/control/test_lean_page.c`, `tests/control/test_html.c` and `tests/control/test_dispatch.c`, rows and checks for removed code may be deleted and indexes and counts may shift; no check of kept behaviour may be removed or loosened.
- The shadow tools (`loop/ctl_shadow.*`, `tests/control/device/shadow*`) stay as they are; if one of them needs a removed function, the builder asks.
- No upstream file changes, and no hook line changes beyond deleting one that only served the mode, named in the result.

## Acceptance criteria

1. `git grep -n -E "frame_log_topic|command_topic|ctl_framelog|CTL_FRAMELOG|no_frame_log" -- HeishaMon tests docs/control ':!docs/control/decisions.md'` prints nothing.
2. A host test loads a `/control.json` holding `frame_log_topic` and `command_topic` with values: it loads, both names are ignored, and every other setting in it is applied.
3. The settings list no longer contains the two names; `docs/control/settings.md` has no row for them.
4. The gate, rows of one test: with the board's own serial port and every input healthy it reports what it reports today; each of its kept reasons still appears for its condition.
5. `make firmware` builds both boards; the D1 mini's global RAM and flash are no larger than on `develop`. Report the before and after numbers.
6. `git diff develop --stat -- HeishaMon ':!HeishaMon/src'` shows no upstream file changed, apart from a deleted hook line the result names.
7. `make check` stays green.
8. docs: `docs/control/firmware.md` and `docs/control/settings.md` describe only the board's own path (the sections about running a board without a pump go); `docs/control/decisions.md` gets one entry: the remote-board mode removed, because the production board runs the loop itself and nothing used it; rejected: keeping it for testing (the host tests and the device replay cover the core, and the dev board can still be flashed as a plain board).

## Smoke test

```sh
make test 2>&1 | grep -E '^control: [0-9]+ test programs passed$' | wc -l
```

Expect: `1`

## Journal
