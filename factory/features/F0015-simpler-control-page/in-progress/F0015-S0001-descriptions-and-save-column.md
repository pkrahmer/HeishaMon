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
  started_at: '2026-11-13T12:27:04.339701Z'
  lease_duration: 60
journal:
- number: 1
  speaker: human
  kind: created
  at: '2026-11-13T12:27:04.333735Z'
  stage: doing
- number: 2
  speaker: factory
  kind: started
  at: '2026-11-13T12:27:04.339701Z'
  stage: doing
---

# Descriptions and the save result per row

## Description

**Why.** On the control page (`/control`), the settings table shows a name and a value field per setting. After a save, the page adds a second table, "Last save", that lists every submitted name again with "accepted" or "refused". The owner finds this confusing: the result sits far from the field it is about, every unchanged setting shows "accepted" too, and nothing says what a setting is for.

**The new settings table.** One row per setting, in today's order, with four columns:

1. **Setting**: the name, as today.
2. **Value**: the field, as today.
3. **What it does**: the description below, in one line or two.
4. **Last save**: empty, except right after a save:
   - `saved` when the save changed this setting's value;
   - `refused: <reason>` when this setting's value was refused, where the reason is `not a valid value` (the setter answered `BAD_VALUE`), `conflicts with other settings` (`INVALID_SET`) or `unknown setting` (`UNKNOWN`), and the field shows the value that is still in force;
   - empty when the value was sent unchanged.

The table "Last save" and anything that only built it go away. A refused row is visibly marked (for example, its text in the page's warning colour); a saved row is not marked beyond the word.

**The descriptions.** Each in the owner's words, with the unit and the allowed values. For a room, the description names the room's number. Before writing them into the code, the builder checks each against the code and the docs; where the code says otherwise, the builder corrects the wording to match the code and lists each correction in its result.

| Setting | What it does |
|---|---|
| `enabled` | Send commands to the pump: 0 = only watch and decide, 1 = control the pump. |
| `cooling` | Allow cooling demand: 0 = heating only, 1 = heating and cooling. |
| `rooms` | Number of room sensors in the indoor average, 0 to 16. Save first; the room fields then appear. |
| `room.<i>.topic` | MQTT topic of room `<i>`'s temperature. |
| `room.<i>.weight` | Weight of room `<i>` in the indoor average, 0 to 1000. |
| `room_timeout_s` | Seconds after which a room's last value counts as too old. |
| `pv_topic` | MQTT topic of the PV power in W, for the solar gains. Empty: no PV. |
| `frame_log_topic` | Another HeishaMon's log topic to read the pump's frames from. Empty: this board's own pump. |
| `desired_indoor_c` | Target for the indoor average, in °C. |
| `transmission_w_per_k` | The house's heat loss per kelvin between inside and outside, in W/K. |
| `adjustment_w_per_k` | Extra power per kelvin the indoor average is off its target, in W/K. |
| `hysteresis_wh` | The energy balance, in Wh, at which the pump is switched on (below minus this) or off (above plus this). |
| `trim_limit_k` | The largest deviation from the target, in K, that the adjustment acts on. `none`: no limit. |
| `solar_gain_factor` | Share of the PV power counted as solar heat entering the house. |
| `internal_w` | Heat from people and devices inside, in W. |
| `heat_cool_heat_value` | The `SetHeatCoolMode` value that means heat; -1 never sends it. Used only without Smart Grid. |
| `min_change_interval_s` | Shortest time between two switches of the pump, in seconds. |
| `retry_interval_s` | Seconds before a command that was not confirmed is sent again. |
| `max_retries` | Repeats of an unconfirmed command before the router reports a fault. |
| `smart_grid` | How the pump is switched: 1 = Smart Grid mode (0 heating allowed, 1 = Capacity 1), 0 = the external thermostat input. |
| `command_topic` | Another HeishaMon's commands topic to send commands to. Empty: this board's own serial port. |

If F0014 has removed a setting from this list, its row is simply absent; a setting this list misses gets a description in the same style, listed in the result.

**Memory.** The descriptions must not take RAM on the ESP8266: they live in flash, as the page's other fixed texts do. The builder states in its result the free heap change it expects, and `make firmware`'s memory lines show the RAM use before and after.

## Interface

- The descriptions are reachable by setting name through one function in the fork's own code, so that a host test can check that every listed setting has one. Its form is the builder's choice; a room's description takes the room's number.
- The leaves for tests another story wrote: `tests/control/test_form.c` and `tests/control/test_html.c` may change where they test the "Last save" table or the settings table's columns. No other test changes. New tests go into new files.
- No upstream file changes, and no hook line changes.

## Acceptance criteria

1. A host test: every name that `ctl_settings_name_at` lists, with `rooms` 2, has a non-empty description, and the two rooms' descriptions name their numbers.
2. A host test of the settings table's rows after a save that changed `hysteresis_wh`, sent `enabled` unchanged and refused `rooms` `99`: the row of `hysteresis_wh` says `saved`, the row of `enabled` has an empty last column, and the row of `rooms` says `refused: not a valid value` and shows the value still in force.
3. The page has no table "Last save" any more: no text `Last save` appears in what the page writes.
4. `make firmware` compiles both boards, and its memory lines for the D1 mini show no more than 200 bytes more of global RAM than on `develop`.
5. `git diff develop --stat -- HeishaMon ':!HeishaMon/src'` shows no upstream file changed.
6. `make check` stays green.
7. docs: `docs/control/firmware.md`, section *The control page*, describes the four columns and the save results, and no longer the "Last save" table.

## Smoke test

```sh
make firmware | grep -c -E '^(esp8266|esp32):'
```

Expect: `2`

## Journal

1. 2026-11-13 human: created.
2. factory: started doing as heishamon.
