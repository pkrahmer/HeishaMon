---
stage: review
blocked: null
round: 0
attempts: 0
crashes: 0
pull_request: 13
comments_read: []
handed_over: null
claim: null
journal:
- number: 1
  speaker: human
  kind: created
  at: '2026-11-11T22:46:29.243427Z'
  stage: doing
- number: 2
  speaker: factory
  kind: started
  at: '2026-11-11T22:46:29.250046Z'
  stage: doing
- number: 3
  speaker: builder
  kind: result
  at: '2026-11-11T22:50:04.687793Z'
  stage: doing
  run: b0565fdc2fa84a48a796540d36271045
---

# The room average

## Description

The core's indoor temperature is one signal, but the house has several room thermostats. This story adds the room average of `docs/control/firmware-design.md` §3.1: a weighted mean over the rooms that are fresh, emitted only while the fresh rooms carry more than half of the total weight. A room whose payload timestamp does not advance is a replay and does not refresh. The glue (a later feature, or the shadow script) hands each room's reading to it as it arrives, and asks it for the average on its own 60 s clock; the average then becomes one `T_INDOOR` reading for the core.

The module is pure: it takes values and instants in seconds of the same monotonic clock the core uses, and it never reads a topic or a clock.

## Interface

New files: `HeishaMon/src/control/adapt/ctl_rooms.h`, `ctl_rooms.c`. Test file: `tests/control/test_rooms.c`. It uses `ctl_payload` and `ctl_bounds` of the story before.

```c
#define CTL_ROOMS_MAX 16

typedef struct {
    size_t count;                     /* rooms configured, 1 to CTL_ROOMS_MAX */
    double weight[CTL_ROOMS_MAX];     /* 0 to 1000; 0 = watched, not counted */
    double room_timeout_s;            /* a room older than this leaves the average */
} ctl_rooms_config;

typedef struct {
    bool has_value[CTL_ROOMS_MAX];
    double value_c[CTL_ROOMS_MAX];
    double t[CTL_ROOMS_MAX];          /* arrival instant of the room's last accepted value */
    bool has_timestamp[CTL_ROOMS_MAX];
    int64_t timestamp_ms[CTL_ROOMS_MAX];
} ctl_rooms;

typedef enum {
    CTL_ROOM_ACCEPTED,
    CTL_ROOM_BAD_INDEX,
    CTL_ROOM_UNPARSED,
    CTL_ROOM_IMPLAUSIBLE,
    CTL_ROOM_REPLAYED
} ctl_room_status;

typedef struct {
    bool has_average;                 /* false: nothing to emit */
    double average_c;
    size_t fresh_count;
    double fresh_weight;
    double total_weight;
} ctl_rooms_average;

void ctl_rooms_config_defaults(ctl_rooms_config *config, size_t count);
bool ctl_rooms_config_valid(const ctl_rooms_config *config);
void ctl_rooms_init(ctl_rooms *rooms);
ctl_room_status ctl_rooms_offer(ctl_rooms *rooms, const ctl_rooms_config *config, size_t index,
                                const char *payload, size_t length, double t);
bool ctl_rooms_is_fresh(const ctl_rooms *rooms, const ctl_rooms_config *config, size_t index, double now);
void ctl_rooms_average_at(const ctl_rooms *rooms, const ctl_rooms_config *config, double now,
                          ctl_rooms_average *average);
```

The configuration:

- `ctl_rooms_config_defaults` sets `count` to its argument, every weight to 1.0, and `room_timeout_s` to 1800.0.
- `ctl_rooms_config_valid` is true when `count` is from 1 to `CTL_ROOMS_MAX`, every weight of the first `count` is accepted by `CTL_BOUND_WEIGHT`, their sum is greater than 0, and `room_timeout_s` is finite and greater than 0.

Offering a reading, in this order:

- `index` not below `count` gives `CTL_ROOM_BAD_INDEX`.
- A payload `ctl_payload_parse` refuses gives `CTL_ROOM_UNPARSED`.
- A value `CTL_BOUND_TEMPERATURE` refuses, or a `t` that is not finite, gives `CTL_ROOM_IMPLAUSIBLE`.
- A payload with a timestamp, for a room whose last accepted payload had a timestamp at least as large, gives `CTL_ROOM_REPLAYED`.
- Otherwise the room takes the value, the arrival instant `t`, and the timestamp or its absence, and the result is `CTL_ROOM_ACCEPTED`.
- Every result but `CTL_ROOM_ACCEPTED` leaves the rooms as they were.
- A payload without a timestamp is always taken when it passes the bounds, also after one with a timestamp.

Freshness and the average:

- A room is fresh at `now` when it has a value and `now - t <= room_timeout_s`. An age equal to the timeout is fresh. A negative age is fresh. A NaN `now` is not fresh.
- `ctl_rooms_average_at` fills `average`:
  - `total_weight` is the sum of the configured weights;
  - `fresh_count` is the number of fresh rooms, weight 0 included;
  - `fresh_weight` is the sum of the fresh rooms' weights.
- When `fresh_weight` is more than half of `total_weight` (strictly), `has_average` is true and `average_c` is the sum of weight times value over the fresh rooms, divided by `fresh_weight`, summed in room order. Otherwise `has_average` is false and `average_c` is 0.0.

## Acceptance criteria

1. The configuration, as rows of one table test:
   - the defaults for 4 rooms are valid, with weights 1.0 and a timeout of 1800.0;
   - refused: a count of 0, a count of 17, a negative weight, a weight of 1000.001, all weights 0, a timeout of 0, a negative timeout and a NaN timeout;
   - accepted: a count of 16, a weight of 1000.0, one weight 0 among others.
2. Offering, as rows of one table test on 4 rooms with the defaults, each row with its status and with the rooms after it:
   - index 4 gives `BAD_INDEX`;
   - `abc` gives `UNPARSED`;
   - `61` and `{"v":-60.5}` give `IMPLAUSIBLE`, and so does a NaN `t`;
   - `{"ts":100,"v":21.0}` gives `ACCEPTED`;
   - then `{"ts":100,"v":22.0}` and `{"ts":99,"v":22.0}` give `REPLAYED`;
   - then `{"ts":101,"v":22.0}` gives `ACCEPTED`;
   - then a bare `23.0` gives `ACCEPTED` and clears the room's timestamp;
   - then `{"ts":50,"v":24.0}` gives `ACCEPTED`, because the last accepted payload had no timestamp.
3. Freshness at the limit, as rows of one table test, for a room accepted at `t` 1000.0 with the timeout 1800.0: fresh at 2800.0, not fresh at the next double above 2800.0, fresh at 500.0, not fresh at NaN; a room never offered is not fresh.
4. The average with equal weights: four rooms accepted at 0.0 with 21.0, 22.0, 23.0 and 24.0, asked at 60.0, gives `has_average` true, `average_c` 22.5, `fresh_count` 4, `fresh_weight` 4.0 and `total_weight` 4.0.
5. The weights: weights 1.0, 2.0, 1.0 and 0.0 with the values of criterion 4 give `average_c` `(1.0*21.0 + 2.0*22.0 + 1.0*23.0) / 4.0`, `fresh_count` 4 and `fresh_weight` 4.0.
6. The half rule, as rows of one table test, four rooms of weight 1.0:
   - with three fresh, `has_average` is true and the average is over those three;
   - with exactly two fresh (`fresh_weight` 2.0 of 4.0, not more than half), `has_average` is false and `average_c` is 0.0;
   - with weights 3.0, 1.0, 1.0 and 1.0 and only the first fresh (3.0 of 6.0), `has_average` is false; and with weights 3.1, 1.0, 1.0 and 1.0 and only the first fresh, it is true.
7. A room leaving and returning: four rooms accepted at 0.0, one of them offered again at 1000.0; asked at 2000.0 only that room is fresh and `has_average` is false; after the other three are offered at 2100.0 and the average is asked at 2100.0, all four are fresh again.
8. `make check` stays green.
9. docs: `docs/control/adapters.md` gets a section on the room average: what it computes, the replay rule and why, the half rule with one example, the defaults, and the three consequences of `docs/control/firmware-design.md` §3.1 in one sentence each. `docs/control/decisions.md` records the replay rule and the timeout as a setting, with the observation that the broker showed room values about 90 minutes old.

## Smoke test

```sh
make test | grep '^control:'
```

Expect: a line `control: N test programs passed`, where N is one more than before this story

## Journal

1. 2026-11-11 human: created.
2. factory: started doing as heishamon.
3. builder: Built the room average.

   - `HeishaMon/src/control/adapt/ctl_rooms.h` and `ctl_rooms.c`: the interface exactly as the story gives it. `ctl_rooms_offer` runs its checks in the story's order: index, then `ctl_payload_parse`, then `CTL_BOUND_TEMPERATURE` and a finite `t`, then the replay. Only an accepted payload writes to the rooms. `ctl_rooms_is_fresh` is true when `now - t <= room_timeout_s`; a NaN `now` compares false, so it is not fresh. `ctl_rooms_average_at` sums the weights, the fresh weights and weight×value in room order, and emits an average only when `fresh_weight > total_weight / 2.0`.
   - `tests/control/test_rooms.c`: one table test or function per criterion, 1 to 7. The offering table runs in sequence on 4 default rooms. After every row it compares every field of all 16 slots with a snapshot, so a refusal that changes any room is caught. I wrote the test first and saw it fail to build before the module existed.
   - Docs:
     - `docs/control/adapters.md` has a new section: what it computes, the replay rule and why, the half rule with the 3-of-4 and 2-of-4 example, the weight-0 rooms, the defaults, and the three §3.1 consequences in one sentence each.
     - `docs/control/decisions.md` has three new entries: the replay rule (with the observation of room values about 90 minutes old on the broker), the timeout as a setting, and how invalid configurations are handled.

   Decisions where the story left things open (all recorded in decisions.md):
   - The stored `timestamp_ms` is 0 when the accepted payload had no timestamp.
   - `ctl_rooms_average_at` returns no average, with all counts and weights at 0, for a configuration that `ctl_rooms_config_valid` refuses. Rejected: making a valid configuration the caller's duty, because a count above 16 would then read past the arrays.
   - `offer` and `is_fresh` also treat an index at or above `CTL_ROOMS_MAX` as out of range, whatever the count says.
   - `ctl_rooms_config_defaults` sets all 16 weights to 1.0, not only the first `count`.
   - A `now` of -infinity counts as fresh, because the story calls any negative age fresh. No test covers it.

   Smoke test: `make test | grep '^control:'` gives `control: 12 test programs passed`, one more than the 11 before. `make check` is green: lint, test and device. The device sketch build is unchanged, since the sketch does not use the new module.

   Noticed but not touched: the glue that publishes weight-0 rooms in telemetry, and the conversion of the average into a `T_INDOOR` reading, are outside this story.

   `check`: check: green
