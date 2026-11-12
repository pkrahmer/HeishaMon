# The telemetry

## Description

The design publishes every input, status, output and setting (`docs/control/firmware-design.md` §6, decision F-7), so that the owner can see what the loop saw, what it decided and why, and what it sent. This story adds the pure C part: for a decision record, the rooms and the settings, it lists the subtopics and their value texts. The glue of the next story prefixes `<mqtt_topic_base>/control/` and publishes them. The loop on the board never reads them back.

The list is produced one entry at a time, by index, so that the glue needs no buffer for all of them at once.

## Interface

New files: `HeishaMon/src/control/glue/ctl_telemetry.h`, `ctl_telemetry.c`. Test file: `tests/control/test_telemetry.c`.

```c
#define CTL_TELEMETRY_SUBTOPIC_MAX 48 /* with its terminating zero */

typedef enum { CTL_GROUP_INPUTS, CTL_GROUP_STATUS, CTL_GROUP_OUTPUTS } ctl_group;

typedef struct {
    const ctl_loop_record *record;
    const ctl_rooms *rooms;
    const ctl_rooms_config *rooms_config;
    double uptime_s;
    bool enabled;
} ctl_telemetry_source;

size_t ctl_telemetry_count(const ctl_telemetry_source *source, ctl_group group);
bool ctl_telemetry_entry(const ctl_telemetry_source *source, ctl_group group, size_t index,
                         char *subtopic, size_t subtopic_size, char *value, size_t value_size);
bool ctl_telemetry_setting(const ctl_settings *settings, size_t index,
                           char *subtopic, size_t subtopic_size, char *value, size_t value_size);
```

The entries, in this order. A number is written by `ctl_format_fixed` with the decimals named; a value the record does not have is written `-`.

Inputs (`inputs/...`):

| Subtopic | Value |
|---|---|
| `inputs/indoor_c` | the decision's indoor input, 2 decimals |
| `inputs/outdoor_c` | outdoor input, 2 decimals |
| `inputs/produced_w` | produced input, 0 decimals |
| `inputs/solar_w` | solar input, 0 decimals |
| `inputs/internal_w` | internal input, 0 decimals |
| `inputs/substituted` | the substituted signals' names joined by `,`, or `-` |
| `inputs/unhealthy` | the unhealthy signals' names joined by `,`, or `-` |
| `inputs/rooms_fresh` | the fresh count |
| `inputs/rooms_fresh_weight` | the fresh weight, 1 decimal |
| `inputs/room/<i>/value_c` | for each configured room: its last value, 2 decimals, or `-` |
| `inputs/room/<i>/fresh` | `1` or `0` at the record's instant |
| `inputs/room/<i>/weight` | its weight, 1 decimal |

Status (`status/...`):

| Subtopic | Value |
|---|---|
| `status/demand`, `status/reason`, `status/phase` | the names of the core's enums |
| `status/energy_wh` | 1 decimal |
| `status/p_net_w` | 0 decimals, or `-` without inputs |
| `status/clamped` | `1` or `0` |
| `status/integrated_s`, `status/skipped_s` | 0 decimals |
| `status/uptime_s` | 0 decimals |
| `status/enabled` | `1` or `0` |

Outputs (`outputs/...`):

| Subtopic | Value |
|---|---|
| `outputs/router` | `IDLE`, `WAITING` or `FAULT` |
| `outputs/thermostat_state` | the router's sent state, or `-` before the first send |
| `outputs/commands` | the record's commands as `name:value` joined by `,`, or `-` |

Settings: `ctl_telemetry_setting` gives, for index `i`, the subtopic `config/<name>` and the value of `ctl_settings_get`, with `ctl_settings_name_at` giving the name. It returns false past the last name.

`ctl_telemetry_entry` returns false for an index past the group's count, and for a buffer too small for the subtopic or the value. It writes nothing then.

Reading `outputs/thermostat_state` needs the router. The source gives the record, and the record holds the commands, not the router. So this story adds `int sent_state` to `ctl_loop_record`, filled by `ctl_loop_tick` from the router after its step. That is a change to the loop's record by the owner's leave. Its test in `tests/control/test_loop.c` gains one row by the same leave, and keeps every other row as it is.

## Acceptance criteria

1. The counts: with 4 rooms, inputs has 9 + 4 × 3 = 21 entries, status 10, outputs 3.
2. Each group's entries, for a record built in the test, as rows of one table test: subtopic and value of every entry. The record has every field set, `substituted` holds `P_SOLAR` and `P_INTERNAL`, and `unhealthy` is empty. Two rooms are fresh, one is stale and one never received a value.
3. A record without inputs writes `-` for the five input values and for `status/p_net_w`.
4. `outputs/thermostat_state` is `-` before the first send and `2` after a send of 2; `outputs/commands` lists two commands as `SetExternalThermostat1State:2,SetHeatCoolMode:1`.
5. The settings entries: index 0 gives `config/enabled` with `0`; the last index gives `config/max_retries` with `3`; the next returns false.
6. A subtopic buffer one byte too small, a value buffer one byte too small and an index past the end each return false and write nothing.
7. `ctl_loop_tick` fills `sent_state`: -1 before the first send, the state after it.
8. `make check` stays green.
9. docs: `docs/control/firmware.md` gets a section *The telemetry* with the three tables, the `config/` subtopics, and the prefix `<mqtt_topic_base>/control/` that the glue adds. `docs/control/loop.md` names the new record field.

## Smoke test

```sh
make test | grep '^control:'
```

Expect: a line `control: N test programs passed`, where N is one more than before this story

## Journal
