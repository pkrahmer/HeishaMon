# Cleanup before the first release

## Goal

The owner is going to flash the fork onto the production HeishaMon by OTA, so that the board on the pump runs the control loop itself with Smart Grid mode. Before that, the firmware loses what was only a bridge on the way: the four pump-value topics of F0009 (`outdoor_topic`, `heat_topic`, `valve_topic`, `defrost_topic`). Since F0011 the dev board takes the pump values from real frames, and the board on the pump takes them from its own frames; nothing uses the topics any more, and a forgotten one would silently cut the loop off from the pump's own frames. The builder also removes code that nothing reaches. Done when the story is merged, `make check` is green, and the docs describe only what the firmware does.

## Scope

- Remove the four pump topics: settings, dispatch, glue, telemetry, tests and docs.
- Remove code that nothing calls or reaches, in the fork's own folders, and list each removal in the result.

## Out of scope

- The thermostat path, the gate, the frame log, the command topic and Smart Grid mode: all stay.
- The host and device test tools under `tests/control/device/` (replay, shadow): they are test tooling, not firmware, and stay.
- Upstream files.

## Stories

- S0001: Remove the pump topics and dead code

## The human's changes

None.

## Decisions taken for the human

1. A `control.json` that still holds one of the four removed settings loads without error: the unknown names are ignored, as any unknown name is.
2. Removing a test that only tested removed code is allowed; every other test stays.
