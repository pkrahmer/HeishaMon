---
stage: doing
blocked: null
round: 1
attempts: 0
crashes: 0
pull_request: 18
comments_read: []
handed_over: null
claim: null
journal:
- number: 1
  speaker: human
  kind: created
  at: '2026-11-12T05:32:49.009931Z'
  stage: doing
- number: 2
  speaker: factory
  kind: started
  at: '2026-11-12T05:32:49.016173Z'
  stage: doing
- number: 3
  speaker: builder
  kind: result
  at: '2026-11-12T05:40:35.177733Z'
  stage: doing
  run: 0763807b24eb407c87248602f8a31091
- number: 4
  speaker: factory
  kind: started
  at: '2026-11-12T05:40:41.904990Z'
  stage: review
- number: 5
  speaker: reviewer
  kind: result
  at: '2026-11-12T05:43:03.890249Z'
  stage: review
  run: 5e0fd0dcba49496caf27d686f4ab9e58
---

# The settings

## Description

The firmware needs one place for everything the owner can set: whether commands are enabled, the room topics and weights, the PV topic, the room timeout, cooling, the core's tuning, and the router's pacing. This story adds that place as pure C under `HeishaMon/src/control/glue/`.

The settings are a flat set of names, each with a text value:

- **One setter** parses and validates a single value, then checks the whole set before it accepts the change. A refused change leaves the settings as they were (`docs/control/firmware-design.md` §6: valid means apply, invalid means reject and keep the old value).
- **One getter** writes any value back as text.
- **One function** turns the settings into the loop's configuration.

The settings file, the MQTT setting topics and a later web page will all go through these functions. The glue of a later story reads and writes the file; this story does no input or output.

## Interface

New files: `HeishaMon/src/control/glue/ctl_settings.h`, `ctl_settings.c`. Test file: `tests/control/test_settings.c`.

```c
#define CTL_SETTINGS_TOPIC_MAX 96   /* the longest topic, without its terminating zero */
#define CTL_SETTINGS_VALUE_MAX 100  /* the longest value text any setting gives, with its zero */
#define CTL_SETTINGS_NAME_MAX 32    /* the longest name, with its zero */

typedef struct {
    bool enabled;
    bool cooling;
    size_t room_count;
    char room_topic[CTL_ROOMS_MAX][CTL_SETTINGS_TOPIC_MAX + 1];
    double room_weight[CTL_ROOMS_MAX];
    double room_timeout_s;
    char pv_topic[CTL_SETTINGS_TOPIC_MAX + 1];
    double desired_indoor_c;
    double transmission_w_per_k;
    double adjustment_w_per_k;
    double hysteresis_wh;
    bool trim_limit_enabled;
    double trim_limit_k;
    double solar_gain_factor;
    double internal_w;
    int heat_cool_heat_value;
    double min_change_interval_s;
    double retry_interval_s;
    int max_retries;
} ctl_settings;

typedef enum { CTL_SETTING_OK, CTL_SETTING_UNKNOWN, CTL_SETTING_BAD_VALUE, CTL_SETTING_INVALID_SET } ctl_setting_status;

void ctl_settings_defaults(ctl_settings *settings);
ctl_setting_status ctl_settings_set(ctl_settings *settings, const char *name, const char *value);
bool ctl_settings_get(const ctl_settings *settings, const char *name, char *value, size_t size);
size_t ctl_settings_name_count(const ctl_settings *settings);
bool ctl_settings_name_at(const ctl_settings *settings, size_t index, char *name, size_t size);
bool ctl_settings_valid(const ctl_settings *settings);
void ctl_settings_to_loop(const ctl_settings *settings, ctl_loop_config *config);
```

The names, in the order `ctl_settings_name_at` lists them:

| Name | Value text | Default | Rule |
|---|---|---|---|
| `enabled` | `0` or `1` | `0` | |
| `cooling` | `0` or `1` | `0` | |
| `rooms` | decimal | `0` | 0 to 16; a lower count keeps the topics and weights above it unlisted |
| `room.<i>.topic` | text | empty | one per room `i` below `rooms`; see topics below |
| `room.<i>.weight` | number | `1.0` | one per room `i` below `rooms`; 0 to 1000 |
| `room_timeout_s` | number | `1800` | greater than 0 |
| `pv_topic` | text | empty | empty means no PV; see topics below |
| `desired_indoor_c` | number | the core's default | as the core validates it |
| `transmission_w_per_k` | number | the core's default | as the core validates it |
| `adjustment_w_per_k` | number | the core's default | as the core validates it |
| `hysteresis_wh` | number | the core's default | as the core validates it |
| `trim_limit_k` | number or `none` | `3` | `none` disables the trim limit |
| `solar_gain_factor` | number | the core's default | as the core validates it |
| `internal_w` | number | `550` | the core's `fallback_w` of `P_INTERNAL`; finite |
| `heat_cool_heat_value` | `-1`, `0` or `1` | `-1` | |
| `min_change_interval_s` | number | `300` | as the router validates it |
| `retry_interval_s` | number | `120` | as the router validates it |
| `max_retries` | decimal | `3` | as the router validates it |

The values:

- A number is parsed by `ctl_payload_parse` as a bare number, and a decimal must be a whole number in its range.
- A topic is empty, or 1 to `CTL_SETTINGS_TOPIC_MAX` printable ASCII characters (0x21 to 0x7e) without `+` and `#`. An empty room topic means a room that never receives a value; an empty `pv_topic` means no PV.
- `ctl_settings_get` writes numbers through `ctl_format_fixed`: 0 decimals for whole numbers, otherwise 3 decimals, with trailing zeros after the point removed, and the point too when nothing follows it. It writes `none` for a disabled trim limit, and the flags and decimals as above. It returns false for an unknown name or a too-small buffer.

The setter, in order:

1. An unknown name, or a room index not below the current `rooms` for `room.<i>.*`, gives `UNKNOWN`.
2. A value that does not parse, or breaks its row's rule, gives `BAD_VALUE`.
3. Otherwise it applies the value to a copy. When the copy fails `ctl_settings_valid`, the result is `INVALID_SET`; otherwise the copy replaces the settings and the result is `OK`.
4. Every result but `OK` leaves the settings unchanged.

The whole set:

- `ctl_settings_valid` is true when the loop configuration made by `ctl_settings_to_loop` passes `ctl_loop_config_valid`.
- With `rooms` at 0, the set is valid and the loop gets 1 room of weight 1 that never receives a value. The core then waits for inputs, and the glue publishes that.

`ctl_settings_to_loop` starts from `ctl_loop_config_defaults` and sets:

- the core's fields from the settings, with `fallback_w` of `P_INTERNAL` from `internal_w`;
- the rooms' count, weights and timeout;
- the router's pacing and polarity;
- `allowed` as `HEATING` and `NONE`, with `COOLING` when `cooling` is 1;
- `assume_landed` false.

## Acceptance criteria

1. The defaults: every name of the table gives its default text through `ctl_settings_get`, the set is valid, and `ctl_settings_name_count` is 16 with `rooms` at 0.
2. The name list follows `rooms`: after `rooms` is set to 2 with topics set, the count is 20, and `ctl_settings_name_at` gives `room.0.topic`, `room.0.weight`, `room.1.topic`, `room.1.weight` after `rooms`, in that place. An index past the end returns false.
3. Each number setting, as rows of one table test: one accepted value and one value just outside its rule, which gives `BAD_VALUE` or `INVALID_SET` as the rule's place says, leaving the settings unchanged.
4. Topics, as rows of one table test:
   - `ccu/device/status/000A9D89A7961A/1/ACTUAL_TEMPERATURE` is accepted;
   - each of these gives `BAD_VALUE`: a topic of 97 characters, `a+b`, `a/#` and `a b`;
   - the empty text is accepted for a room and for `pv_topic`.
5. Rooms, in order on the defaults: `room.0.topic` gives `UNKNOWN` while `rooms` is 0; `rooms 2` gives `OK` with two empty topics and weights 1; then `room.0.topic` and `room.1.topic` give `OK`; `rooms 17` gives `BAD_VALUE`; `room.1.weight 0` then `room.0.weight 0` gives `OK` and then `INVALID_SET`, because all weights 0 fail the rooms' validation.
6. `trim_limit_k`: `none` disables the limit and `get` writes `none`; `2.5` enables it and `get` writes `2.5`; `0` gives `BAD_VALUE`.
7. The formatting of `get`, as rows of one table test: 1500 gives `1500`, 0.02 gives `0.02`, 22.5 gives `22.5`, 805 gives `805`, and a buffer one byte too small returns false.
8. `ctl_settings_to_loop` with the defaults gives a configuration equal, field by field, to `ctl_loop_config_defaults` for one room, except that `allowed` is `HEATING` and `NONE` and `assume_landed` is false. With `cooling` 1 it also allows `COOLING`; with `internal_w` 400 the core's `P_INTERNAL` fallback is 400.0.
9. An unknown name gives `UNKNOWN`, and `ctl_settings_get` of it returns false.
10. `make check` stays green.
11. docs: `docs/control/firmware.md` is new, with a section *The settings*: the table above, the rule that a refused change keeps the old value, and why `rooms` 0 is valid. `docs/control/decisions.md` records the flat name set and the default of commands disabled.

## Smoke test

```sh
make test | grep '^control:'
```

Expect: a line `control: N test programs passed`, where N is one more than before this story

## Journal

1. 2026-11-12 human: created.
2. factory: started doing as heishamon.
3. builder: Built the settings as pure C99 in the new folder `HeishaMon/src/control/glue/`.

   **What I built**
   - `HeishaMon/src/control/glue/ctl_settings.h` and `ctl_settings.c` provide the interface the story names.
     - Names are found in a fixed table of 16. The `room.<i>.topic` and `room.<i>.weight` names are parsed separately and listed after `rooms`.
     - The setter applies the value to a copy, runs `ctl_settings_valid` on it, and replaces the settings only on OK.
     - Defaults come from `ctl_config_defaults`, `ctl_rooms_config_defaults` and `ctl_router_config_defaults`, so no number is written twice.
     - `ctl_settings_to_loop` starts from `ctl_loop_config_defaults` with one room when `rooms` is 0.
   - `tests/control/test_settings.c` covers every criterion with table tests. It also covers:
     - texts that do not parse: a JSON object, `1e12`, `0.0004`, `2.5` for a decimal, NULL;
     - bad room names: a leading zero, a missing index, a wrong suffix, an index of 16 or more;
     - topics with a tab and with 0x7f;
     - a lower `rooms` count keeping what lies above it, which comes back when the count rises again;
     - a buffer one byte too small for both names and values;
     - every setting reaching its field in the loop's configuration.
   - I checked that the tests catch faults by breaking the code on purpose: dropping the whole-set check, the round-trip check, the JSON refusal and the cooling bit each makes the test fail.
   - Docs:
     - `docs/control/firmware.md` is new, with the section *The settings*: the table, the value rules, the rule that a refused change keeps the old value, and why `rooms` 0 is valid.
     - `docs/control/decisions.md` has three new entries: the flat name set, commands disabled by default, and the getter-exact numbers below.

   **Decisions where the story was open** (recorded in decisions.md)
   - **Where a rule is checked:** a rule that belongs to the row itself gives BAD_VALUE (flags, `rooms` 0–16, room weight via `CTL_BOUND_WEIGHT`, `room_timeout_s` > 0, `trim_limit_k` > 0, `heat_cool_heat_value` −1..1, topics). Rules the core, rooms or router check give INVALID_SET, so each rule lives in one place. So `max_retries 11` and `desired_indoor_c 30.001` give INVALID_SET.
   - **Numbers the getter can write back exactly:** a number must be below 1e12 in magnitude (the limit of `ctl_format_fixed`) and have at most 3 decimals, or it is BAD_VALUE. Otherwise a value saved through the getter would read back changed at the next boot. Rejected: rounding on set.
   - **Bare numbers only:** a JSON object is refused, even though `ctl_payload_parse` accepts one.
   - **Decimals:** the flags and `heat_cool_heat_value` go through the same parse as other decimals, so `1.0` is accepted. `max_retries` accepts any whole number that fits an `int`, and the router decides its range.
   - **Room names:** the index is written without leading zeros (`room.00.topic` is UNKNOWN). A NULL name is UNKNOWN; a NULL value is BAD_VALUE.

   **Smoke test:** `make test | grep '^control:'` gives `control: 19 test programs passed`; there were 18 before. `make check` is green.

   **Noticed, not touched**
   - `ctl_settings_set` keeps a full copy of the settings on the stack (about 1.9 KB, mostly the 16 room topics), as the story's copy-then-validate rule asks. That is a lot for the ESP8266's 4 KB stack. The glue story should call it from a shallow stack, or we decide to shorten the topics.
   - `write_value` casts away `const` to reuse `number_field`; there is a comment on it. A separate const accessor would duplicate the switch.
   - No sketch includes `glue/` yet, so `make device` compiles the new folder as a library but builds none of its code for the board.

   `check`: check: green
4. factory: started review as heishamon.
5. reviewer: Reviewed `git diff develop...HEAD` (glue/ctl_settings.h/.c, test_settings.c, firmware.md, decisions.md) against the story, and checked it against ctl_config.c, ctl_rooms.c, ctl_router.c, ctl_payload.h and ctl_format.h. The code matches the Interface. Bounds, buffer sizes and the copy-then-validate setter hold. The diff touches nothing outside the fork's folders. The factory's `make check` after the builder was green. Earlier tests: none, because the diff only adds `tests/control/test_settings.c`.

   Findings:
   1. `tests/control/test_settings.c` test_rooms, `CHECK_TRUE(!ctl_settings_get(&settings, "room.1.topic", NULL, 0))`: the getter returns false for a NULL buffer whatever the name. So the Interface rule "a lower count keeps the topics and weights above it unlisted" has no real test for its unlisted side. Fix: pass a real buffer, and also check that `ctl_settings_name_count` is 18 and that `room.1.topic` gives UNKNOWN from the setter after `rooms 1`.
   2. `tests/control/test_settings.c` test_numbers: only the upper limit of `rooms` (0 to 16) is tested, with 16 and 17. Setting 0 from a non-zero count is never tested as accepted, and `-1` is never tested as BAD_VALUE. Fix: add the row `{"rooms", "0", "-1", CTL_SETTING_BAD_VALUE}`.
   3. Work item `## Description`, *The values*: the builder added a rule that a number must have a magnitude below 1e12 and at most 3 decimals, or it is BAD_VALUE (`parse_number` in `ctl_settings.c`; recorded in decisions.md and firmware.md). The Description does not state it, so it no longer says what the setter accepts. Fix: add the rule to the Description's list of values.
   4. `tests/control/test_settings.c` test_unparsed_numbers: the 1e12 limit is tested only on its refused side (`1e12`). Fix: add an accepted row just below the limit, e.g. `hysteresis_wh 999999999999`, and check that the getter writes it back unchanged.
   5. `docs/control/firmware.md`, the paragraph "A refused change keeps the old value": it says "A rule the core, the rooms or the router check is checked only there". That is false: the setter's `number_in_rule` in `ctl_settings.c` also checks the room weight (`CTL_BOUND_WEIGHT`) and `room_timeout_s > 0`, which `ctl_rooms_config_valid` checks too. The comment on `number_in_rule`, "so that each rule lives in one place", makes the same false claim. Fix: reword both to say the setter checks each row's own rule, as the story's table gives it (weight, timeout, `trim_limit_k` > 0, the ranges, topics), and leaves the remaining rules to the core, the rooms and the router.

   Not raised: the setter's ~1.9 KB stack copy, which the story's copy-then-validate rule requires and which the builder flagged for the glue story. The const cast in `write_value`, which has a comment on it.
