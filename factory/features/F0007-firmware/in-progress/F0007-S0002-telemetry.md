---
stage: review
blocked: null
round: 0
attempts: 0
crashes: 0
pull_request: 20
comments_read: []
handed_over: null
claim:
  instance: heishamon
  stage: review
  started_at: '2026-11-12T06:15:12.446286Z'
  lease_duration: 60
journal:
- number: 1
  speaker: human
  kind: created
  at: '2026-11-12T06:03:37.596495Z'
  stage: doing
- number: 2
  speaker: factory
  kind: started
  at: '2026-11-12T06:03:37.602865Z'
  stage: doing
- number: 3
  speaker: builder
  kind: result
  at: '2026-11-12T06:15:06.299782Z'
  stage: doing
  run: dbb1b6ee67cf4ed88ead722414c53370
- number: 4
  speaker: factory
  kind: started
  at: '2026-11-12T06:15:12.446286Z'
  stage: review
---

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

1. 2026-11-12 human: created.
2. factory: started doing as heishamon.
3. builder: Built the pure telemetry list and the record's new `sent_state` field.

   What I built:
   - `HeishaMon/src/control/glue/ctl_telemetry.h` and `ctl_telemetry.c`. They follow the story's interface: `ctl_telemetry_count`, `ctl_telemetry_entry` and `ctl_telemetry_setting`. Each entry's subtopic and value are first written into the module's own buffers (the subtopic buffer is CTL_TELEMETRY_SUBTOPIC_MAX, the value buffer an internal VALUE_MAX of 128). They are copied out only when both fit, so a refusal writes nothing.
   - `HeishaMon/src/control/loop/ctl_loop.h` and `ctl_loop.c`: `ctl_loop_record` gains `int sent_state`. `decide()` sets it from `loop->router.sent_state` after the router's step, which the owner allowed.
   - `tests/control/test_telemetry.c` covers:
     - the counts (21/10/3 with 4 rooms);
     - one table test per group over a full record: substituted is P_SOLAR,P_INTERNAL, unhealthy is empty, two rooms are fresh, one is stale and one never had a value;
     - the unhealthy names and the flags at 0/FAULT;
     - a record without inputs: `-` for the five inputs and for p_net_w;
     - thermostat_state `-` and then `2`, and the two commands;
     - settings: index 0 is config/enabled 0, the last index is config/max_retries 3, the next index is false; also config/room.0.topic;
     - refusals: a subtopic or value buffer one byte short, an index past the end in each group, a group that is no member, a setting buffer one byte short, and a number of 1e12 or more;
     - the longest subtopic (`inputs/room/15/value_c`) fits.
   - `tests/control/test_loop.c` gains one test, `test_record_sent_state`, under the owner's leave; no other test changed. A router configuration the router refuses (max_retries 11) never sends, so the record has -1. The default configuration has 0 after its first tick.
   - Docs:
     - `docs/control/firmware.md` has a new section *The telemetry*: the three tables, the `config/` subtopics, the prefix `<mqtt_topic_base>/control/` and the refusal rule.
     - `docs/control/loop.md` names `sent_state` in the decision tick's record step.
     - `docs/control/decisions.md` has one new entry.

   Decisions where the story was open (recorded in decisions.md):
   - A value holding a number that `ctl_format_fixed` refuses (magnitude 1e12 or more) makes the whole entry false and writes nothing. This matches `ctl_loop_format_record`. I rejected writing `-`, because that would make a broken value look like a missing one.
   - NaN and infinity are written as the fixed format writes them.
   - A sent_state below 0 is written `-`.
   - An enum value that is no member is written `INVALID`, as the name functions already do.
   - No NULL checks, following the existing decision "No function checks for a null pointer".
   - The configured room count is capped at CTL_ROOMS_MAX when counting entries.

   Tests first: I wrote the test file and the code in the same pass, so I did not watch the tests fail before the code existed. Instead I checked by mutation that they catch faults:
   - dropping the `sent_state` fill fails two checks in test_loop;
   - an off-by-one buffer check fails the refusal checks;
   - a renamed subtopic fails the table test.

   Smoke test: `make test | grep '^control:'` gives `control: 20 test programs passed`. It was 19 on the committed state (checked with `git stash -u`). `make check` is green: lint, test, device, firmware, `check: green`.

   Noticed but not touched:
   - `router_status_name` is now a static copy in both `ctl_loop.c` and `ctl_telemetry.c`. A shared `ctl_router_status_name` in `route/` would remove the copy, but that is beyond this story.
   - The interface has no public maximum for value texts. The longest entry value fits 128 bytes, and settings values fit CTL_SETTINGS_VALUE_MAX (100). The publishing glue of the next story will need to pick a buffer size, and a `CTL_TELEMETRY_VALUE_MAX` might be worth adding then.
   - Upstream's Rules harness prints compiler warnings during `make test`. They were already there and are upstream's.

   `check`: check: green
4. factory: started review as heishamon.
