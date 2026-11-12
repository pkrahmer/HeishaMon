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
