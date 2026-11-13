# Remove the pump topics and dead code

## Description

**Why.** The fork goes onto the owner's production HeishaMon next. The four pump-value topics of F0009 were a bridge for a board without a pump: `outdoor_topic`, `heat_topic`, `valve_topic` and `defrost_topic`. While any of them is set, the frame hook hands nothing to the loop. Since F0011 a board without a pump reads real frames from another board's log, and a board on a pump reads its own frames. Nothing needs the topics now, and a leftover value in a `control.json` would cut the loop off from the pump. This story removes them, and removes code in the fork's own folders that nothing calls or reaches.

**The pump topics.** After this story:
- the four settings do not exist: not in the name list, not on the control page, not in the `config` telemetry, not in `/control.json` when it is written;
- a `/control.json` that still holds any of them loads as if they were absent, like any unknown name today, and the other settings in it keep their values;
- the dispatch has no pump-topic kinds, and the glue subscribes to no pump topic;
- the frame hook always hands the pump values from frames to the loop, from the board's own pump or from the frame log;
- `ctl_settings_pump_from_mqtt` and everything only it served are gone.

**Dead code.** In `HeishaMon/src/control/`, `tests/control/` (except `tests/control/device/`) and `docs/control/`, the builder removes:
- functions, types, macros and constants that nothing uses after the removal above;
- branches that cannot be reached;
- comments and doc lines about removed code.

The builder lists each removal outside the pump topics in its result, with one line on why nothing reaches it. Something a header documents as part of a module's contract and that a test uses is not dead, even if the firmware does not call it. When in doubt, the builder keeps it and lists it as a doubt.

**What stays.** The thermostat path, the gate, the frame log, `command_topic`, `smart_grid`, the telemetry of every remaining setting and input, the control page for every remaining setting, and everything under `tests/control/device/`.

## Interface

- Settings: the four names leave the name list. The name count with `rooms` 0 becomes 19.
- The leaves for tests another story wrote: the builder may change `tests/control/test_settings.c`, `tests/control/test_dispatch.c`, `tests/control/test_telemetry.c`, `tests/control/test_form.c` and `tests/control/test_html.c` only to remove rows about the four settings and to update counts and positions that the removal shifts. It may delete a whole test file only when every check in it is about removed code. No other test changes.
- New test: `tests/control/test_settings_legacy.c` loads a `control.json` text holding `"heat_topic":"a/b"` and `"outdoor_topic":"c/d"` together with `"hysteresis_wh":1200`, and checks that loading succeeds, `hysteresis_wh` is 1200, and a written settings text holds neither removed name.
- No upstream file changes, and no hook line changes.

## Acceptance criteria

1. `git grep -nE "outdoor_topic|heat_topic|valve_topic|defrost_topic|pump_from_mqtt" -- HeishaMon/src/control tests/control` prints nothing, except lines of `tests/control/test_settings_legacy.c`.
2. `tests/control/test_settings_legacy.c` passes as the Interface says.
3. The name count is 19.
4. The result lists every removal outside the pump topics, and every doubt kept.
5. `make firmware` compiles both boards, and `git diff develop --stat -- HeishaMon ':!HeishaMon/src'` shows no upstream file changed.
6. `make check` stays green.
7. docs: `docs/control/firmware.md` no longer describes the four settings or a board that takes pump values from MQTT. Its section *Shadowing another HeishaMon* now points to *Frames from another HeishaMon's log* as the way to feed a board without a pump. `docs/control/decisions.md` gets one entry: the pump topics removed, why, and that the frame log replaces them; earlier entries stay as they are.

## Smoke test

```sh
make firmware | grep -c -E '^(esp8266|esp32):'
```

Expect: `2`
