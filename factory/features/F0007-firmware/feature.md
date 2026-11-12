# The control loop inside the firmware

## Goal

The loop runs on the D1 as a shadow, fed by a PC. This feature puts it inside the HeishaMon firmware itself, for both boards, as `docs/control/firmware-design.md` §2 to §6 describe:

- the pump values come from the board's own decoder;
- the rooms and the PV come from MQTT;
- the decisions go to the optional PCB emulation through upstream's command function;
- the settings live in one file of their own;
- everything is published under the fork's own subtree.

The firmware starts with commands disabled. It decides and publishes, but it sends nothing to the pump until the owner sets `enabled`. A board flashed by mistake onto the production pump therefore only shadows. It is for the owner, who wants one device that holds the wire to the pump, the control law and the network link. Done when every story below is merged, `make check` is green (including the firmware build for both boards), and the owner has flashed it.

## Scope

- The settings: a flat set of names and values, each validated, with the whole set checked before it is applied, and conversion to the loop's configuration.
- The telemetry: the topics and values of the design's §6 (inputs, status, outputs, config), as pure C.
- The glue (`.cpp`): it loads and saves the settings file, reads the four pump values after each decoded frame, subscribes and dispatches the room, PV and setting topics, ticks the loop, publishes the telemetry, and sends the router's commands when enabled.
- The hook lines in upstream's `HeishaMon.ino`: one include and five calls.
- `docs/control/firmware.md`.

## Out of scope

- The web page for the settings: a later feature, once the settings are proven.
- Enabling the loop on the owner's pump: the owner's step, after the hand tests of `docs/control/firmware-design.md` §9, items 6 and 7.
- Any change to upstream's settings, topics or behaviour beyond the hook lines.

## Stories

The build order; start one at a time, the next when the one before is archived in `done/`.

- S0001: The settings
- S0002: The telemetry
- S0003: The glue and the hook lines

## The human's changes

Made before S0001 by the session in the owner's seat:

- `factory/stage.Containerfile` adds the ESP32 core 3.3.11 and HeishaMon's libraries, pinned.
- The `Makefile` adds `make firmware`, which compiles `HeishaMon/HeishaMon.ino` for the D1 mini and the ESP32-S3 with upstream's board options, and joins `check`. The pipeline's time limit for `check` grows to match.
- `CLAUDE.md` says so, and gives the builder the leave for the hook lines in `HeishaMon/HeishaMon.ino` that S0003 names, and nothing else in upstream's files.

## Decisions taken for the human

1. Commands are disabled by default (`enabled` false). The loop then decides and publishes, but sends nothing.
2. The settings are a flat set of names, so the file, the MQTT setting topics and a later web page all use one setter with one validation.
3. The settings file is the fork's own `/control.json`; upstream's `config.json` is not touched (decision F-5: only configuration is written to flash).
4. Everything is published under `<mqtt_topic_base>/control/`, and settings are changed through `<mqtt_topic_base>/control/set/<name>`. The trust level is the one the pump's command topics already have (§6 of the design).
5. The default room timeout stays the design's 1800 s, although the owner's CCU3 reports a room only on a change. The value is a setting, and the owner chooses it for the house.
