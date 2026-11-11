---
stage: review
blocked: null
round: 3
attempts: 0
crashes: 0
pull_request: 13
comments_read: []
handed_over: null
claim:
  instance: heishamon
  stage: review
  started_at: '2026-11-11T23:00:22.961820Z'
  lease_duration: 60
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
- number: 4
  speaker: factory
  kind: started
  at: '2026-11-11T22:50:10.295454Z'
  stage: review
- number: 5
  speaker: reviewer
  kind: result
  at: '2026-11-11T22:51:32.649621Z'
  stage: review
  run: 694d8aa53daa4987befb4b5f97b37290
- number: 6
  speaker: factory
  kind: started
  at: '2026-11-11T22:51:38.148830Z'
  stage: doing
- number: 7
  speaker: builder
  kind: result
  at: '2026-11-11T22:53:39.483238Z'
  stage: doing
  run: 1d20c21a855a4802803114d66db08bef
- number: 8
  speaker: factory
  kind: started
  at: '2026-11-11T22:53:46.810045Z'
  stage: review
- number: 9
  speaker: reviewer
  kind: result
  at: '2026-11-11T22:55:14.970378Z'
  stage: review
  run: 1e6095e0f57b4c8cb1f88ea3393dea5e
- number: 10
  speaker: factory
  kind: started
  at: '2026-11-11T22:55:21.004602Z'
  stage: doing
- number: 11
  speaker: builder
  kind: result
  at: '2026-11-11T22:57:06.730037Z'
  stage: doing
  run: 279a5cf568b2438396ce6969e87cdc06
- number: 12
  speaker: factory
  kind: started
  at: '2026-11-11T22:57:11.763477Z'
  stage: review
- number: 13
  speaker: reviewer
  kind: result
  at: '2026-11-11T22:58:21.768604Z'
  stage: review
  run: 4064750ecd1244f2af9dacf07b9a2302
- number: 14
  speaker: factory
  kind: started
  at: '2026-11-11T22:58:26.940155Z'
  stage: doing
- number: 15
  speaker: builder
  kind: result
  at: '2026-11-11T23:00:17.349580Z'
  stage: doing
  run: e54f0301f0964ec9937df9ad1967fc99
- number: 16
  speaker: factory
  kind: started
  at: '2026-11-11T23:00:22.961820Z'
  stage: review
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
    int64_t timestamp_ms[CTL_ROOMS_MAX];  /* 0 when the last accepted payload had none */
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

- `ctl_rooms_config_defaults` sets `count` to its argument, every weight (all `CTL_ROOMS_MAX` of them) to 1.0, and `room_timeout_s` to 1800.0.
- `ctl_rooms_config_valid` is true when `count` is from 1 to `CTL_ROOMS_MAX`, every weight of the first `count` is accepted by `CTL_BOUND_WEIGHT`, their sum is greater than 0, and `room_timeout_s` is finite and greater than 0.

Offering a reading, in this order:

- `index` not below `count`, or not below `CTL_ROOMS_MAX` whatever `count` says, gives `CTL_ROOM_BAD_INDEX`.
- A payload `ctl_payload_parse` refuses gives `CTL_ROOM_UNPARSED`.
- A value `CTL_BOUND_TEMPERATURE` refuses, or a `t` that is not finite, gives `CTL_ROOM_IMPLAUSIBLE`.
- A payload with a timestamp, for a room whose last accepted payload had a timestamp at least as large, gives `CTL_ROOM_REPLAYED`.
- Otherwise the room takes the value, the arrival instant `t`, and the timestamp or its absence (a `timestamp_ms` of 0 when there is none), and the result is `CTL_ROOM_ACCEPTED`.
- Every result but `CTL_ROOM_ACCEPTED` leaves the rooms as they were.
- A payload without a timestamp is always taken when it passes the bounds, also after one with a timestamp.

Freshness and the average:

- A room is fresh at `now` when it has a value and `now - t <= room_timeout_s`. An age equal to the timeout is fresh. A negative age is fresh. A NaN `now` is not fresh. An index not below `count`, or not below `CTL_ROOMS_MAX` whatever `count` says, is not fresh.
- `ctl_rooms_average_at` fills `average`:
  - `total_weight` is the sum of the configured weights;
  - `fresh_count` is the number of fresh rooms, weight 0 included;
  - `fresh_weight` is the sum of the fresh rooms' weights.
- When `fresh_weight` is more than half of `total_weight` (strictly), `has_average` is true and `average_c` is the sum of weight times value over the fresh rooms, divided by `fresh_weight`, summed in room order. Otherwise `has_average` is false and `average_c` is 0.0.
- For a configuration `ctl_rooms_config_valid` refuses, `ctl_rooms_average_at` gives `has_average` false and `average_c`, `fresh_count`, `fresh_weight` and `total_weight` all 0.

## Acceptance criteria

1. The configuration, as rows of one table test:
   - the defaults for 4 rooms are valid, with weights 1.0 and a timeout of 1800.0;
   - refused: a count of 0, a count of 17, a negative weight, a weight of 1000.001, all weights 0, a timeout of 0, a negative timeout, a NaN timeout and an infinite timeout;
   - accepted: a count of 1, a count of 16, a weight of 1000.0, one weight 0 among others.
2. Offering, as rows of one table test on 4 rooms with the defaults, each row with its status and with the rooms after it:
   - index 4 gives `BAD_INDEX`;
   - `abc` gives `UNPARSED`;
   - `61` and `{"v":-60.5}` give `IMPLAUSIBLE`, and so do a NaN `t` and an infinite `t`;
   - rows that fail two checks show the order: index 4 with `abc` gives `BAD_INDEX`, and `abc` with a NaN `t` gives `UNPARSED`;
   - `{"ts":100,"v":21.0}` gives `ACCEPTED`;
   - then `{"ts":100,"v":61}`, and `{"ts":100,"v":22.0}` with an infinite `t`, give `IMPLAUSIBLE`, not `REPLAYED`;
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
8. Out of range, on rooms that all hold a value at 0.0:
   - with 4 rooms, `ctl_rooms_is_fresh` at 0.0 is true for index 3 and false for index 4;
   - with a count of 17, `ctl_rooms_offer` for index 16 gives `BAD_INDEX` and leaves the rooms as they were, and `ctl_rooms_is_fresh` for index 16 is false;
   - `ctl_rooms_average_at` with a count of 17, or with a weight of -1.0, gives `has_average` false and `average_c`, `fresh_count`, `fresh_weight` and `total_weight` all 0.
9. `make check` stays green.
10. docs: `docs/control/adapters.md` gets a section on the room average: what it computes, the replay rule and why, the half rule with one example, the defaults, and the three consequences of `docs/control/firmware-design.md` §3.1 in one sentence each. `docs/control/decisions.md` records the replay rule and the timeout as a setting, with the observation that the broker showed room values about 90 minutes old.

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
4. factory: started review as heishamon.
5. reviewer: Review, round 0. Scope: `git diff develop...HEAD`, without `factory/`.

   Findings:

   1. `tests/control/test_rooms.c` `test_config` rows: no test covers the failing side of "`room_timeout_s` is finite". The NaN row would still be refused without the `isfinite` check in `ctl_rooms_config_valid` (`ctl_rooms.c:37`), because `NaN > 0.0` is false, so a `+INFINITY` timeout is never tested. Criterion 1 does not list it either, so this is also a gap in the story. Fix: add a row `{TIMEOUT, INFINITY, false}` and name it in criterion 1.
   2. `tests/control/test_rooms.c` `test_config` rows: the count limit is tested only on its upper side (16 accepted, 17 refused). On the lower side, 0 is refused but no row shows that a count of 1 is accepted. This is also a gap in criterion 1. Fix: add a row `{COUNT, 1.0, true}` and name it in criterion 1.
   3. `tests/control/test_offer` in `test_rooms.c`: the rule "a `t` that is not finite gives `CTL_ROOM_IMPLAUSIBLE`" is tested only with NaN. A check written as `t == t` would pass every row while still accepting an infinite `t`. Criterion 2 names only NaN, so this is also a gap in the story. Fix: add a row with `t = INFINITY` that gives `IMPLAUSIBLE` and leaves the rooms as they were, and name it in criterion 2.
   4. `tests/control/test_rooms.c` `test_offer`: the Interface says the checks of `ctl_rooms_offer` run "in this order", but every refused row fails exactly one check, so swapping two checks in `ctl_rooms.c:69-82` passes every row. No criterion covers the order, so this is also a gap in the story. Fix: add rows that fail two checks at once, for example index 4 with `abc` giving `BAD_INDEX`, and `{"ts":100,"v":61}` after `ts` 100 was accepted giving `IMPLAUSIBLE` rather than `REPLAYED`, and name them in criterion 2.
   5. `ctl_rooms.c:113-115` (`ctl_rooms_average_at` returns zeros for an invalid configuration) and `ctl_rooms.c:93` (`ctl_rooms_is_fresh` returns false for an index not below `count`): the builder added these behaviours, and `ctl_rooms.h` and decisions.md promise them, but no test exercises them. Fix: test both, for example `average_at` with `count` 17 or a weight of -1 giving `has_average` false and `total_weight` 0.0, and `is_fresh` with index 4 on 4 rooms giving false.
   6. The work item's `## Description`, "Freshness and the average": it says `total_weight` "is the sum of the configured weights". For a configuration that `ctl_rooms_config_valid` refuses, the code gives 0.0 instead, and the Description does not mention the `CTL_ROOMS_MAX` index bound or `timestamp_ms` 0 without a timestamp. The journal changed the meaning, but the Description was not rewritten. Fix: add these three decisions to the Description's Interface, or ask the human if they are not wanted.

   Checked and in order:
   - Criteria 1 to 7: each has its test, and each test checks what its criterion says. The boundaries checked are 2800.0 against the next double above, 2 of 4 and 3.0 of 6.0 against 3.1 of 6.1, and `ts` 100, 99 and 101. The offering table compares all 16 slots after every row.
   - The factory's `make check` after the builder was green.
   - Architecture: new files only under `adapt/`, and they include only same-folder headers and the standard C headers. Names use `ctl_`/`CTL_`, helpers are `static`, all numbers are `double`, and no Python reference exists for this module.
   - Embedded safety: no dynamic memory and no recursion. Every loop is bounded by `CTL_ROOMS_MAX`, or by `count` after it was validated. Indexes are guarded against `CTL_ROOMS_MAX`, `t` is checked with `isfinite`, and NaN `now` is handled.
   - Docs: adapters.md and decisions.md cover what criterion 9 asks for, and every claim in them matches the code.
   - Earlier tests: none. The diff adds `tests/control/test_rooms.c` (status A) and changes no existing test file.
6. factory: started doing as heishamon.
7. builder: Round 1: fixed the six review findings. I changed no code. `HeishaMon/src/control/adapt/ctl_rooms.c` and `.h` already behaved as the new tests expect.

   - `tests/control/test_rooms.c`:
     - `test_config` (findings 1 and 2): new rows for a timeout of `INFINITY`, refused, and a count of 1, accepted.
     - `test_offer` (finding 3): a new row where `t = INFINITY` gives `IMPLAUSIBLE`.
     - `test_offer` (finding 4): new rows that fail two checks at once:
       - index 4 with `abc` gives `BAD_INDEX`;
       - `abc` with a NaN `t` gives `UNPARSED`;
       - after `ts` 100 is accepted, `{"ts":100,"v":61}` gives `IMPLAUSIBLE`, and `{"ts":100,"v":22.0}` with an infinite `t` also gives `IMPLAUSIBLE`, not `REPLAYED`.
     - Each new row also checks that all 16 slots are unchanged afterwards.
     - New `test_out_of_range` (finding 5): `ctl_rooms_is_fresh` with index 4, or with `CTL_ROOMS_MAX`, on 4 rooms gives false; index 3 on the same rooms gives true. `ctl_rooms_average_at` with a count of 17, or with a weight of -1.0, gives `has_average` false and every count and weight 0.
   - Mutation check, on a temporary copy of the module: the new rows fail when `!isfinite(t)` is replaced by `t != t`, and when the replay check is moved before the bounds check. The unchanged module passes.
   - Work item Description (finding 6):
     - The Interface now states that an index not below `CTL_ROOMS_MAX` is out of range for both offer and freshness, that `timestamp_ms` is 0 when the payload had none, and that an invalid configuration gives all-false and all-zero averages.
     - Criteria 1 and 2 name the new rows.
     - A new criterion 8 covers out of range. `make check` and docs are now criteria 9 and 10.
     - `docs/control/decisions.md` already recorded these decisions, so I did not change it.

   Smoke test: `make test | grep '^control:'` gives `control: 12 test programs passed`, one more than the 11 before the story. `make check` is green.

   Noticed but not touched: nothing new.

   The factory left out changes outside the write paths: factory/features/F0004-adapters/in-progress/F0004-S0002-room-average.md.

   `check`: check: green
8. factory: started review as heishamon.
9. reviewer: Review, round 1. Scope: `git diff develop...HEAD -- . ':!factory'`, plus the work item.

   Findings:

   1. `tests/control/test_rooms.c` `test_out_of_range` and `test_offer`: no test checks the failing side of the `CTL_ROOMS_MAX` guard in `index_valid` (`ctl_rooms.c`, `index < CTL_ROOMS_MAX`). `ctl_rooms.h` and decisions.md both promise this guard holds "whatever the count says". The one row that tries it (`ctl_rooms_is_fresh` with index `CTL_ROOMS_MAX` on 4 rooms) is already refused by `index < count`. Without the guard, every test still passes, and a count of 17 then reads and writes slot 16, past the arrays. Fix: with `config.count = 17`, check that `ctl_rooms_offer` for index 16 gives `CTL_ROOM_BAD_INDEX` and leaves the rooms as they were, and that `ctl_rooms_is_fresh` for index 16 gives false. The Description's criterion 8 now names these checks.

   Work item Description:
   - The builder's round-1 rewrite of the Description was dropped. The work item is not in the builder's write paths, so round 0's finding 6 was still open.
   - The reviewer's write paths hold the work item, so I rewrote the `## Description` to match what is built and what decisions.md records:
     - `timestamp_ms` is 0 when the payload had no timestamp.
     - The defaults set all `CTL_ROOMS_MAX` weights.
     - The index rule covers `CTL_ROOMS_MAX` whatever the count says, for both offer and freshness.
     - `ctl_rooms_average_at` gives all zeros for an invalid configuration.
     - Criterion 1 adds count 1 and an infinite timeout.
     - Criterion 2 adds an infinite `t` and the two-failure order rows.
     - A new criterion 8 covers out-of-range cases, including the count-17 checks of finding 1.
     - `make check` and docs are now criteria 9 and 10.
   - I changed no code and no test.

   Checked and in order:
   - Acceptance criteria:
     - Criteria 1 to 7 each have a table test or function that checks what the criterion says.
     - Round 0's findings 1 to 5 are closed by the new rows: count 1, infinite timeout, infinite `t`, the two-failure rows (`BAD_INDEX` over `UNPARSED`, `UNPARSED` over `IMPLAUSIBLE`, `IMPLAUSIBLE` over `REPLAYED`), `is_fresh` for index 3 and 4, and `average_at` with count 17 or weight -1.
     - Every offer row compares all 16 slots.
     - Boundaries tested on both sides: 2800.0 against the next double above; 2 of 4 and 3.0 of 6.0 against 3.1 of 6.1; weight 0/1000 against -1/1000.001; count 0/1 and 16/17; `ts` 99, 100 and 101.
     - The journal says the factory's `make check` after the builder was green.
   - Architecture: new files only under `HeishaMon/src/control/adapt/`, `tests/control/` and `docs/control/`. Includes are same-folder headers and standard C headers only. Public names use `ctl_`/`CTL_`, helpers are `static`, all numbers are `double`, and errors come back as status values.
   - Embedded safety:
     - No dynamic memory, no VLA and no recursion.
     - Loops are bounded by `CTL_ROOMS_MAX`, or by `count` after `ctl_rooms_config_valid`.
     - `t` is checked with `isfinite`, and the payload value with `CTL_BOUND_TEMPERATURE`.
     - A NaN `now` is handled.
   - Quality: names are clear, there is no dead code, and the module reuses `ctl_payload_parse` and `ctl_bounds_accept` without copying them. Comments give reasons.
   - Docs:
     - The adapters.md section covers everything criterion 10 asks for, and its claims match the code: weight 0 counts in `fresh_count`, a timestamp-less payload clears the timestamp, and the defaults are 1.0 and 1800 s.
     - decisions.md records the replay rule with the 90-minute observation, the timeout as a setting, and the handling of invalid configurations.
     - The three consequences match `firmware-design.md` §3.1.
   - Earlier tests: none. `tests/control/test_rooms.c` is the only test file in the diff, and it is new (status A).
10. factory: started doing as heishamon.
11. builder: Round 2: fixed the one finding from the round-1 review. I changed no code. The guard in `index_valid` (`HeishaMon/src/control/adapt/ctl_rooms.c`) already behaved as the new checks expect.

    - `tests/control/test_rooms.c`, `test_out_of_range`: a new block uses the rooms that all hold a value at 0.0 and sets `config.count` to 17 (`CTL_ROOMS_MAX + 1`). It checks three things:
      - `ctl_rooms_offer` for index 16 with `22.0` gives `CTL_ROOM_BAD_INDEX`;
      - the rooms are unchanged afterwards, compared with a snapshot over all 16 slots using the existing `check_rooms_equal`;
      - `ctl_rooms_is_fresh` for index 16 gives false.
      This is criterion 8's count-17 row, as the reviewer wrote it into the Description.
    - Mutation check on a temporary copy of the module: I removed `&& index < CTL_ROOMS_MAX` from `index_valid`. The new block then fails at lines 189 and 191 (status ACCEPTED, and slots changed by the write past the array). The unchanged module passes. In that mutant the out-of-bounds write is undefined behaviour, but the checks still caught it.
    - Description and docs: unchanged. The reviewer already rewrote the Description in round 1. `docs/control/decisions.md` already records the `CTL_ROOMS_MAX` guard.

    Smoke test: `make test | grep '^control:'` gives `control: 12 test programs passed`, one more than the 11 before the story. `make check` is green: lint, test and device.

    Noticed but not touched: nothing new.

    `check`: check: green
12. factory: started review as heishamon.
13. reviewer: Review, round 2. Scope: `git diff develop...HEAD -- . ':!factory'`, plus the work item. I read the code, the test and the two documents, and checked the docs against `docs/control/firmware-design.md` §3.1 and the bounds in `adapt/ctl_bounds.h`.

    Findings:

    1. `tests/control/test_rooms.c` `test_config`, first block: the Interface says `ctl_rooms_config_defaults` sets "every weight (all `CTL_ROOMS_MAX` of them)" to 1.0. The test checks only `weight[0..3]`. Every later use either overwrites slots 4 to 15 (the count-16 row) or uses an invalid configuration (count 17). As a result, a `ctl_rooms_config_defaults` that sets only the first `count` weights passes every test. Criterion 1 only says "with weights 1.0", so this is also a gap in the story. Fix: check `defaults.weight[i] == 1.0` for every `i < CTL_ROOMS_MAX`, and make criterion 1 say "all 16 weights 1.0".
    2. `tests/control/test_rooms.c` `test_config` rows: the Interface says only "every weight of the first `count`" must be accepted by `CTL_BOUND_WEIGHT`, but no row puts a refused weight at or after index `count`. The loop sets slots 4 to 15 to 1.0 after every row. As a result, a `ctl_rooms_config_valid` that checks all `CTL_ROOMS_MAX` weights (`ctl_rooms.c`, the loop over `config->count`) passes every test. The sum's limit is covered (the all-weights-0 row keeps slots 4 to 15 at 1.0), but the bound check's limit is not. This is also a gap in criterion 1. Fix: add a row with count 4 and `weight[4] = -1.0` (or 1000.001) that is accepted, and name it in criterion 1 ("a refused weight beyond `count` is ignored").

    Checked and in order:
    - Acceptance criteria:
      - Criteria 1 to 8 each have a test that checks what the criterion says.
      - Round 1's finding is closed: with count 17, `ctl_rooms_offer` for index 16 gives `BAD_INDEX`, all 16 slots stay unchanged, and `ctl_rooms_is_fresh` for index 16 is false.
      - The order of the offer checks is shown by the two-failure rows.
      - Each limit is tested on both sides: 2800.0 against the next double above; 2 of 4 and 3.0 of 6.0 against 3.1 of 6.1; weights 0 and 1000 against -1 and 1000.001; counts 0 and 1, 16 and 17; `ts` 99, 100 and 101; index 3 and 4.
      - The journal says the factory's `make check` after round 2 was green.
    - Architecture: new files only under `HeishaMon/src/control/adapt/`, `tests/control/` and `docs/control/`. Includes are same-folder headers and standard headers only. Public names use `ctl_`/`CTL_`, the helpers are `static`, all numbers are `double`, and errors are status values. The header and function comments are present.
    - Embedded safety: no dynamic memory, no VLA and no recursion. Loops are bounded by `CTL_ROOMS_MAX`, or by `count` after it was validated. Every index is guarded against `CTL_ROOMS_MAX`. `t` is checked with `isfinite`, the value against `CTL_BOUND_TEMPERATURE`, and a NaN `now` gives false.
    - Quality: names are clear, there is no dead code, `ctl_payload_parse` and `ctl_bounds_accept` are reused, and the comments give reasons.
    - Work item hygiene:
      - The Description matches what was built.
      - The adapters.md section covers what criterion 10 asks. Its three consequences match firmware-design.md §3.1: the 600 s `T_INDOOR` limit, the setpoint fitted against the plain mean, and the jump of a few tenths of a kelvin.
      - decisions.md records the replay rule with the 90-minute observation, and the timeout as a setting.
      - Every claim in both documents matches the code.
    - Earlier tests: none. `git diff --name-status` shows `tests/control/test_rooms.c` as A, and no other test file is in the diff.

    Notes:
    - `tests/control/test_rooms.c` `test_offer`: `check_rooms_equal(__FILE__, __LINE__, ...)` reports the loop's line, so a failure does not name its row. Printing `i` alongside would help.
14. factory: started doing as heishamon.
15. builder: Round 3: fixed both findings from the round-2 review. I changed no code: `HeishaMon/src/control/adapt/ctl_rooms.c` already does what the new checks expect.

    - `tests/control/test_rooms.c`, `test_config`:
      - Finding 1: the defaults block now checks `weight[i] == 1.0` for every `i < CTL_ROOMS_MAX`. Before, it checked only the first 4.
      - Finding 2: a new row kind, `WEIGHT_BEYOND_COUNT`, sets `weight[4]` on a count of 4. The rows `-1.0` and `1000.001` are both accepted. To make room for it, I moved the loop that sets slots 4 to 15 to 1.0 (needed for the count-16 row) so it runs before a row's change instead of after it. Every other row works the same way as before.
    - Mutation check on temporary copies of the module:
      - A `ctl_rooms_config_defaults` that sets only the first `count` weights fails at the defaults check, line 41.
      - A `ctl_rooms_config_valid` whose bound check runs over all `CTL_ROOMS_MAX` weights fails at the new rows, line 97.
      - The unchanged module passes.
    - Description: not rewritten. The work item is outside the builder's write paths, so my round-1 rewrite was dropped. Criterion 1 should now say "all 16 weights 1.0" and "a refused weight beyond `count` is ignored", as the round-2 review asked; the reviewer, who may write the work item, would need to add that wording. `docs/control/decisions.md` needs no change, because it already records that only the first `count` weights are configured and that the defaults set all 16.
    - I did not act on the reviewer's note about `check_rooms_equal` naming the loop line instead of the row. It was a note, not a finding, and the check helpers have no message argument to carry the row number.

    Smoke test: `make test | grep '^control:'` gives `control: 12 test programs passed`, one more than the 11 before this story. `make check` is green: lint, test and device.

    Noticed but not touched: nothing new.

    `check`: check: green
16. factory: started review as heishamon.
