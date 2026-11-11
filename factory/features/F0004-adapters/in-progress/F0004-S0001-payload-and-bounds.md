---
stage: review
blocked: null
round: 0
attempts: 0
crashes: 0
pull_request: 12
comments_read: []
handed_over: null
claim: null
journal:
- number: 1
  speaker: human
  kind: created
  at: '2026-11-11T22:11:50.555678Z'
  stage: doing
- number: 2
  speaker: factory
  kind: started
  at: '2026-11-11T22:11:50.561945Z'
  stage: doing
- number: 3
  speaker: builder
  kind: result
  at: '2026-11-11T22:16:12.239905Z'
  stage: doing
  run: f55a7e9fccda4c009244178674967baf
---

# The payload parser and the plausibility bounds

## Description

The adapters receive MQTT payloads as text: the CCU3 publishes a room temperature as a JSON object such as `{"ts":1794430802765,"v":21.1,"s":0}`, and HeishaMon publishes its values as bare numbers such as `5` or `3200`. This story adds the parser that turns such a payload into a number and, where the payload carries one, a timestamp; and the plausibility bounds every adapter applies before a value reaches the core (`docs/control/algorithm-plan.md` §2.5: temperatures from -60 °C to 60 °C, non-negative gains). It is the first part of the adapters; the room average and the pump signals of the next stories use both.

The parser is not general JSON: it reads exactly the shapes these sources send, refuses everything else, and never allocates.

## Interface

New files: `HeishaMon/src/control/adapt/ctl_payload.h`, `ctl_payload.c`, `ctl_bounds.h`, `ctl_bounds.c`. Test files: `tests/control/test_payload.c`, `tests/control/test_bounds.c`. The `.c` files include headers of other folders by a relative path, as `CLAUDE.md` says.

```c
/* ctl_payload.h */
#define CTL_PAYLOAD_MAX 256 /* the longest payload parsed; a longer one is refused */

typedef struct {
    double value;
    bool has_timestamp;
    int64_t timestamp_ms; /* the payload's "ts", when it has one */
} ctl_payload;

bool ctl_payload_parse(const char *text, size_t length, ctl_payload *payload);

/* ctl_bounds.h */
typedef enum {
    CTL_BOUND_TEMPERATURE, /* -60.0 to 60.0 degC, both included */
    CTL_BOUND_HEAT,        /* 0.0 to 30000.0 W, both included */
    CTL_BOUND_PV,          /* 0.0 to 100000.0 W, both included */
    CTL_BOUND_WEIGHT       /* 0.0 to 1000.0, both included */
} ctl_bound;

bool ctl_bounds_accept(ctl_bound bound, double value);
```

The parser:

- `text` holds `length` bytes, not zero-terminated. A `length` above `CTL_PAYLOAD_MAX` is refused.
- Spaces, tabs, `\r` and `\n` before and after the payload are skipped.
- **A bare number**: an optional `-`, digits, an optional `.` with at least one digit after it, an optional exponent (`e` or `E`, an optional sign, digits). At least one digit before the point. No `+` before the number, no leading point, no `nan`, no `inf`, no hexadecimal. It gives the number and no timestamp.
- **A JSON object**: `{`, members separated by `,`, `}`; spaces, tabs, `\r` and `\n` allowed between the tokens.
  - Each member is a key in double quotes without escapes, `:`, and a value.
  - The value of `"v"` or `"value"` is a number in the form above, and is the payload's value.
  - The value of `"ts"` is an integer of digits only, at most 18 of them, and is the timestamp.
  - Other members are skipped when their value is a number, `true`, `false`, `null`, or a string in double quotes without escapes.
  - An object without a value member is refused. So is one with both `"v"` and `"value"`, one with a key twice, one with a nested object or array, and one with anything after its closing `}` but the spaces above.
- The number is converted with `strtod` after the form is checked, and refused when the result is not finite.
- On refusal the function returns false and leaves `*payload` unchanged.

The bounds:

- `ctl_bounds_accept` is true when the value is finite and within the bound's range, both ends included. It is false for NaN, infinities, values outside, and a `bound` that is no member of the enum.

## Acceptance criteria

1. Bare numbers accepted, as rows of one table test: `5` gives 5.0; `-3.5` gives -3.5; `3200` gives 3200.0; `21.10` gives 21.1; `1e3` gives 1000.0; `2.5E-1` gives 0.25; ` 22.7\n` gives 22.7; `0` gives 0.0. None has a timestamp.
2. Bare numbers refused, as rows of one table test, each leaving a marker-filled `ctl_payload` unchanged: the empty text, ` `, `+5`, `.5`, `5.`, `1e`, `1e+`, `--1`, `0x10`, `nan`, `inf`, `1e999` (not finite), `5 6`, `5,6`, `abc`, `"5"`.
3. Objects accepted, as rows of one table test:
   - `{"ts":1794430802765,"v":21.1,"s":0}` gives 21.1 with timestamp 1794430802765.
   - `{"v":22}` gives 22.0 without timestamp.
   - `{"value":-4.5}` gives -4.5.
   - `{ "v" : 1.5 , "unit" : "C", "ok": true, "x": null, "y": false }` gives 1.5.
   - `{"s":0,"v":0,"ts":0}` gives 0.0 with timestamp 0.
4. Objects refused, as rows of one table test, each leaving the marker unchanged:
   - structure: `{}`, `{"ts":5}`, `{"v":1,"value":2}`, `{"v":1,"v":2}`, `{"v":"21.1"}`, `{"v":1,"n":{"a":1}}`, `{"v":1,"n":[1]}`, `{"v":1} x`, `{"v":1`, `{v:1}`, `{"v":1,}`;
   - escapes: `{"v\"":1}`, `{"v":1,"u":"a\"b"}`;
   - timestamps: `{"v":1,"ts":-5}`, `{"v":1,"ts":1.5}`, `{"v":1,"ts":1234567890123456789}` (19 digits).
5. The length limit: a payload of exactly `CTL_PAYLOAD_MAX` bytes (a number padded with spaces) is parsed; one of `CTL_PAYLOAD_MAX + 1` bytes is refused. Text after `length` is not read: `"22.7xyz"` with a length of 4 gives 22.7.
6. The bounds, as rows of one table test, on both sides of every limit:
   - temperature: -60.0 and 60.0 accepted, the next doubles beyond them refused;
   - heat: 0.0 and 30000.0 accepted, -0.001 and 30000.001 refused;
   - PV: 0.0 and 100000.0 accepted, the doubles beyond refused;
   - weight: 0.0 and 1000.0 accepted, the doubles beyond refused;
   - NaN and both infinities refused for every bound;
   - a bound value of 4 refused.
7. `make check` stays green, and `make device` compiles with the new folder.
8. docs: `docs/control/adapters.md` is new. It says in one paragraph what the adapters are and that they are pure, then has a section on the payload parser (the accepted shapes with one example each, and what is refused) and a section on the bounds (a table of the four bounds). `docs/control/decisions.md` records the decisions of this story: a parser for these shapes only instead of a general JSON library, and `strtod` after the form check.

## Smoke test

```sh
make test | grep '^control:'
```

Expect: a line `control: N test programs passed`, where N is two more than before this story

```sh
make device | grep -c '^\. '
```

Expect: `2`

## Journal

1. 2026-11-11 human: created.
2. factory: started doing as heishamon.
3. builder: Built the payload parser and the plausibility bounds. Tests came first and failed (the header was missing), then the code made them pass. After the last edit `make check` ended `check: green`.

   **Paths**
   - `HeishaMon/src/control/adapt/ctl_payload.h`, `ctl_payload.c`: the parser. It walks the text with a cursor bounded by `length`. A hand-written form check (`scan_number`) runs first, then `strtod` converts those bytes, copied into a zero-terminated stack buffer of `CTL_PAYLOAD_MAX + 1`, and a result that is not finite is refused. The timestamp is read as 1 to 18 digits into an `int64_t`, so it cannot overflow. Keys seen are kept as spans in a fixed array of `CTL_PAYLOAD_MAX / 4` entries; the shortest member, `"":0`, takes 4 bytes, so no payload holds more. The result goes into a local and is copied to `*payload` only on success.
   - `HeishaMon/src/control/adapt/ctl_bounds.h`, `ctl_bounds.c`: a `switch` maps each bound to its range. A value outside the enum has no range and is refused, so a cast integer cannot index past a table.
   - `tests/control/test_payload.c`: one table test each for criteria 1 to 4, plus the length test for criterion 5. Every refused row checks that a marker-filled `ctl_payload` is unchanged, field by field (not `memcmp`, because of struct padding).
   - `tests/control/test_bounds.c`: for each of the four bounds, both limits, the doubles just beyond them (`nextafter`), NaN and both infinities. Also heat at -0.001 and 30000.001, and bound 4.
   - `docs/control/adapters.md` (new), and three entries appended to `docs/control/decisions.md`.

   **Decisions where the story left room** (all recorded in `docs/control/decisions.md`)
   - A parser for these shapes only, instead of a general JSON library (ArduinoJson or a C parser).
   - `strtod` after the form check, instead of `strtod` alone or a hand-written conversion. The entry notes that the firmware never calls `setlocale`, so `.` is the decimal separator.
   - `"ts"` allows leading zeros (digits only, as the story says); I rejected JSON's no-leading-zero rule.
   - Unspecified edge cases:
     - Leading zeros are also allowed in bare numbers.
     - A string member may hold any byte except `"` and `\`.
     - A skipped numeric member only has its form checked, so `"x":1e999` is skipped, not refused.

   **Smoke test**
   - `make test | grep '^control:'` prints `control: 11 test programs passed`. That is 9 before plus 2, as expected.
   - `make device | grep -c '^\. '` prints `2`, as expected.

   **Noticed, not touched**
   - `make device` passes with the new `adapt/` folder given as a library. But arduino-cli compiles only the libraries a sketch includes, and `replay.ino` does not include the new headers, so the board compile does not actually build the adapter sources yet. I tried a throwaway probe sketch in a temp folder to compile them for the D1 mini, and the permission prompt denied it. Proving they compile for the board would mean an include in `replay.ino` or a second sketch under `tests/control/device/`; I kept that out of this story's scope.
   - Stack use on the ESP8266 when parsing an object is about 512 bytes for the key spans plus a 257-byte conversion buffer. That fits the 4 KB stack but is worth knowing for the callers.
   - `make test` prints compiler warnings from upstream's rules harness, and `test_types.c` prints expected-failure lines. Both are existing output that still passes, and I left them alone.

   `check`: check: green
