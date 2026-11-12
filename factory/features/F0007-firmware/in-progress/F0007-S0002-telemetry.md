---
stage: doing
blocked: null
round: 2
attempts: 0
crashes: 0
pull_request: 20
comments_read: []
handed_over: null
claim: null
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
- number: 5
  speaker: reviewer
  kind: result
  at: '2026-11-12T06:16:31.390554Z'
  stage: review
  run: 348c1bef3e00484594044243824bf447
- number: 6
  speaker: factory
  kind: started
  at: '2026-11-12T06:16:37.135682Z'
  stage: doing
- number: 7
  speaker: builder
  kind: result
  at: '2026-11-12T06:23:42.556961Z'
  stage: doing
  run: 124fd9a8eb754b2493caa945c5045a37
- number: 8
  speaker: factory
  kind: started
  at: '2026-11-12T06:23:49.397820Z'
  stage: review
- number: 9
  speaker: reviewer
  kind: result
  at: '2026-11-12T06:24:41.566692Z'
  stage: review
  run: 29fd212c296f4cecb6b1b84b86a944a9
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
5. reviewer: Reviewed `git diff develop...HEAD` against the story. I read the code rather than relying on the journal's account. Criteria 1 to 7 have tests. The factory's `make check` after the builder was green. The refusal paths are bounded and copy out only when both texts fit, there is no dynamic memory, and every array index is capped by `room_count` ≤ CTL_ROOMS_MAX.

   Findings:

   1. `HeishaMon/src/control/glue/ctl_telemetry.c:28-141`: `text_writer`, `put_text`, `put_bounded`, `put_number`, `put_input`, `put_commands` and `router_status_name` are second copies of the static helpers in `HeishaMon/src/control/loop/ctl_loop.c:197-300`. The checklist does not allow a second copy of an existing helper, and the builder flagged only `router_status_name`. Fix: move the text writer (start, put text, bounded put, number) into one shared module, for example `loop/ctl_text.h/.c` with `ctl_`-prefixed names. Make the router's status name public in `route/` as `ctl_router_status_name`. Then use both from `ctl_loop.c` and `ctl_telemetry.c`.
   2. `tests/control/test_loop.c:442-444`: the "after the first send" half of `test_record_sent_state` checks only `sent_state == 0`. It does not show that a send happened, and 0 is also what a zeroed field holds. Fix: also check that `record.commands` holds `SetExternalThermostat1State` with value `"0"`, so the check witnesses "the state after it". Alternatively, drive a send of a non-zero state.
   3. `tests/control/test_telemetry.c` (`test_longest_subtopic_fits`, `test_refusals`): `ctl_telemetry.h:29` states two count rules that are each tested on only one side. The CTL_ROOMS_MAX cap is tested at a count of 16 but not above it. "0 for a group that is no member" is never checked through `ctl_telemetry_count`; only the entry's refusal for group 3 is tested. Fix: add a check that `rooms_config.count = CTL_ROOMS_MAX + 1` gives 9 + 3 × CTL_ROOMS_MAX inputs, and a check that `ctl_telemetry_count(&f.source, (ctl_group)3)` is 0.

   Earlier tests:
   - `tests/control/test_loop.c` (M): changed by the story's own leave, which says the owner allows one added row. It adds one test, `test_record_sent_state`, and its call in `main`; every earlier test is unchanged.
6. factory: started doing as heishamon.
7. builder: Round 1: I fixed the reviewer's three findings and nothing else.

   1. Duplicated helpers:
      - New shared module `HeishaMon/src/control/loop/ctl_text.h` / `ctl_text.c`. It has the type `ctl_text` and the functions `ctl_text_start`, `ctl_text_put`, `ctl_text_put_bounded`, `ctl_text_put_number`, `ctl_text_put_input` and `ctl_text_put_commands`. The commands function writes the list or `-`; the record line adds its own `cmd=` key before it.
      - New public `ctl_router_status_name` in `HeishaMon/src/control/route/ctl_router.h` / `.c`.
      - `HeishaMon/src/control/loop/ctl_loop.c` and `HeishaMon/src/control/glue/ctl_telemetry.c` now use both. Their static copies are gone, and so are their direct includes of `ctl_format.h`.
      - The shared writer keeps its text terminated after every piece. `ctl_text_start` marks a size of 0 as not fitting and never writes to it.
      - `ctl_loop_format_record` behaves as before. It still empties the text when it refuses. The record-text table tests in `test_loop.c` pass unchanged.
   2. `tests/control/test_loop.c`, `test_record_sent_state`: the "after the first send" half now also checks that the record holds exactly one command, `SetExternalThermostat1State` with value `0`. So the check shows that a send happened. This is still within the one test the owner allowed; no other test changed.
   3. `tests/control/test_telemetry.c` gains two tests:
      - `test_room_count_is_capped`: a room count of CTL_ROOMS_MAX + 1 gives 9 + 3 × CTL_ROOMS_MAX inputs.
      - `test_no_group_has_no_entries`: `ctl_telemetry_count` for `(ctl_group)3` is 0.
      Both checks held against the committed code. They pin down rules that were already implemented; they do not show a bug.

   Docs:
   - `docs/control/decisions.md` has a new entry, "One text writer for the record line and the telemetry".
   - `docs/control/loop.md` gains one sentence in *The record's text*: the line is built by the shared writer of `ctl_text.h`, and the router status is written by `ctl_router_status_name`.

   Decisions:
   - The writer lives in `loop/`, beside `ctl_format`, because the loop already owned it and `glue/` already includes `loop/` headers.
   - I rejected a separate test program for `ctl_text`. The record and telemetry table tests already cover every function of it through the public API, and a new program would also change the smoke test's count.
   - I added no test for `ctl_router_status_name` in `test_router.c`, because that test belongs to another story. The names IDLE, WAITING and FAULT are still checked through the loop's and the telemetry's tests.

   Smoke test: `make test | grep '^control:'` gives `control: 20 test programs passed`, one more than the 19 before this story. `make check`: lint, test, device and firmware are all green, ending in `check: green`.

   Noticed, not touched: the earlier note still stands that the interface has no public maximum for value texts (`CTL_TELEMETRY_VALUE_MAX`); the publishing glue will need one.

   `check`: check: green
8. factory: started review as heishamon.
9. reviewer: Round 1 review. I read `git diff develop...HEAD` against the story and checked the round-0 fixes in the code.

   The three earlier findings are fixed:
   - `ctl_text` and `ctl_router_status_name` are now shared by `ctl_loop.c` and `ctl_telemetry.c`, and the static copies are gone.
   - `test_record_sent_state` now shows that a send happened.
   - The room-count cap and the count for a group outside the enum are each tested on both sides.

   The rest of the checklist:
   - **Criteria:** criteria 1 to 7 each have a test that checks what the criterion says. The refusal sizes are tested at exact fit and at one byte short, for entries and for settings. The journal records a green `make check`.
   - **Architecture:** every change stays inside the fork's folders, every new public name has the `ctl_` prefix, and the `.c` files are pure C99.
   - **Embedded safety:** there is no dynamic memory or recursion. Every array index is bounded by `room_count` ≤ CTL_ROOMS_MAX or by CTL_ROUTER_COMMANDS_MAX. Every buffer is written with its size, and the writer never writes to a buffer of size 0.
   - **Docs:** I checked the doc claims (enum names, the order of the settings table, `sent_state`, the prefix) against the code.

   Findings:

   1. `HeishaMon/src/control/route/ctl_router.c:65-66`: the new `ctl_router_status_name` was inserted below the comment that belongs to `add_command`. That comment is about the name being a literal shorter than the field, the value being one digit, and the array being bounded by its own size. It now sits above the status-name function, which it does not describe, and `add_command` has lost its comment. Fix: move `ctl_router_status_name` above that comment, or below `add_command`, so the comment sits directly above `add_command` again.

   Earlier tests:
   - `tests/control/test_loop.c` (M): changed under the story's leave ("Its test in `tests/control/test_loop.c` gains one row by the same leave"). It only adds `test_record_sent_state` and its call in `main`. Every earlier test is unchanged.
