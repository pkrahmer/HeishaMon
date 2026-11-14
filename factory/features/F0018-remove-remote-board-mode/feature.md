# Remove the remote-board mode

## Goal

F0011 and F0013 let a board without a pump run the control loop against another HeishaMon: it read the other board's frames from its MQTT log (`frame_log_topic`) and sent its commands to the other board's command topics (`command_topic`). That was the bridge for the full-day test before the owner flashed the fork onto the production board. Since then the production board runs the loop on its own serial port, and nothing uses the mode: about 260 lines of log parsing, its branches in the firmware glue and the gate, and their tests. The owner decided to remove it, as F0014 removed the pump topics. Done when the story is merged, `make check` is green, and the docs describe only what the firmware does.

## Scope

- Remove the two settings `frame_log_topic` and `command_topic`, the frame-log parsing (`glue/ctl_framelog.*`), the MQTT command path, the gate's reasons for a remote board, and their telemetry, tests and docs.
- Remove code that only the mode reached, and list each removal in the result.

## Out of scope

- The shadow tools (`loop/ctl_shadow.*`, `tests/control/device/shadow*`): test tooling, decided separately.
- Smart Grid mode, the thermostat path, the router and the integral term: all stay.
- Upstream files.

## Stories

- S0001: Remove the remote-board mode

## The human's changes

None. The write paths were checked: the story stays in `HeishaMon/src/control/`, `tests/control/` and `docs/control/`.

## Decisions taken for the human

1. A `/control.json` that still holds `frame_log_topic` or `command_topic` loads without error: unknown names are ignored, as since F0014.
2. Removing a test that only tested removed code is allowed (`test_framelog.c`, `test_framelog_sent.c` and any other that tests only the mode); every other test stays, and a test that also covers kept behaviour keeps those checks.
3. The production board's behaviour does not change: with both topics empty, which is its configuration, it does exactly what it does today.
