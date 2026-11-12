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
