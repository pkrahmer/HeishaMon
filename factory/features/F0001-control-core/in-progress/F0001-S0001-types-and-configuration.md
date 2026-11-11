---
stage: review
blocked: null
round: 1
attempts: 0
crashes: 0
pull_request: 3
comments_read: [6805629969]
handed_over: null
claim: null
journal:
- number: 1
  speaker: human
  kind: created
  at: '2026-11-11T19:35:35.855687Z'
  stage: doing
- number: 2
  speaker: factory
  kind: started
  at: '2026-11-11T19:35:35.867346Z'
  stage: doing
- number: 3
  speaker: builder
  kind: result
  at: '2026-11-11T19:39:27.255646Z'
  stage: doing
  run: 7ac65157a1154913940af53a19208d67
- number: 4
  speaker: factory
  kind: started
  at: '2026-11-11T19:39:33.038140Z'
  stage: review
- number: 5
  speaker: reviewer
  kind: result
  at: '2026-11-11T19:40:45.085489Z'
  stage: review
  run: 23714b90e6a14163b6b2e94c8c6ceac7
- number: 6
  speaker: factory
  kind: started
  at: '2026-11-11T19:40:50.388507Z'
  stage: doing
- number: 7
  speaker: builder
  kind: question
  at: '2026-11-11T19:41:15.757532Z'
  stage: doing
  run: 85ad97290cb24b99b05d01b4596c99d9
  blocked: question
- number: 8
  speaker: human
  kind: comment
  at: '2026-11-11T19:47:08.960193Z'
  stage: doing
- number: 9
  speaker: factory
  kind: started
  at: '2026-11-11T19:47:11.416410Z'
  stage: doing
- number: 10
  speaker: builder
  kind: result
  at: '2026-11-11T19:47:48.581823Z'
  stage: doing
  run: bec77441ca8a4159b8877ebd8c8ef3b7
---

# The core's enums and its configuration with validation

## Description

The control core gets its first two modules in plain C99: the four enums every later module speaks in (demand, phase, reason, signal), each with its name as text, and the configuration with its defaults and its validation. The owner needs them first because every other part of the core takes the configuration as an argument, and because the board must refuse a bad configuration before it applies it.

The modules are a port of `docs/control/python-reference/types.py` (the four enums only) and `docs/control/python-reference/config.py`. `docs/control/reference.md` sections 2.1 and 3 describe them. Where this story and the Python differ, this story wins; the differences are listed under *Interface*.

The story also adds the shared check helper that every test program of the control code uses.

Left to later stories: the value types (reading, inputs, balance, ledger, decision), the signal table, the balance, the relay and the controller. Nothing is wired into the firmware.

## Interface

New files: `HeishaMon/src/control/core/ctl_types.h`, `ctl_types.c`, `ctl_config.h`, `ctl_config.c`. Test files: `tests/control/check.h`, `tests/control/check.c`, `tests/control/test_types.c`, `tests/control/test_config.c`.

```c
/* ctl_types.h */
typedef enum { CTL_DEMAND_HEATING, CTL_DEMAND_COOLING, CTL_DEMAND_NONE } ctl_demand;
typedef enum { CTL_PHASE_UNINITIALIZED, CTL_PHASE_RUNNING, CTL_PHASE_HOLD, CTL_PHASE_FAULT } ctl_phase;
typedef enum {
    CTL_REASON_NORMAL, CTL_REASON_SEEDED, CTL_REASON_WAITING_FOR_INPUTS, CTL_REASON_MASKED,
    CTL_REASON_HELD_STALE_INPUT, CTL_REASON_FAULT_TIMEOUT, CTL_REASON_FAULT_RECOVERED,
    CTL_REASON_INVALID_TIME
} ctl_reason;
typedef enum {
    CTL_SIGNAL_T_INDOOR, CTL_SIGNAL_T_OUTDOOR, CTL_SIGNAL_P_PRODUCED,
    CTL_SIGNAL_P_SOLAR, CTL_SIGNAL_P_INTERNAL, CTL_SIGNAL_COUNT
} ctl_signal;

const char *ctl_demand_name(ctl_demand demand);
const char *ctl_phase_name(ctl_phase phase);
const char *ctl_reason_name(ctl_reason reason);
const char *ctl_signal_name(ctl_signal signal);
bool ctl_signal_is_required(ctl_signal signal);

/* ctl_config.h */
typedef struct {
    double desired_indoor_c;
    double transmission_w_per_k;
    double adjustment_w_per_k;
    double hysteresis_wh;
    bool trim_limit_enabled;
    double trim_limit_k;
    double solar_gain_factor;
    double energy_clamp_factor;
    double max_step_s;
    double tick_interval_s;
    double integration_interval_s;
    double input_hold_s;
    double max_age_s[CTL_SIGNAL_COUNT];
    double fallback_w[CTL_SIGNAL_COUNT];
} ctl_config;

typedef enum {
    CTL_CONFIG_BAD_DESIRED_INDOOR = 1u << 0,
    CTL_CONFIG_BAD_TRANSMISSION = 1u << 1,
    CTL_CONFIG_BAD_ADJUSTMENT = 1u << 2,
    CTL_CONFIG_BAD_HYSTERESIS = 1u << 3,
    CTL_CONFIG_BAD_TRIM_LIMIT = 1u << 4,
    CTL_CONFIG_BAD_SOLAR_GAIN = 1u << 5,
    CTL_CONFIG_BAD_ENERGY_CLAMP = 1u << 6,
    CTL_CONFIG_BAD_MAX_STEP = 1u << 7,
    CTL_CONFIG_BAD_TICK_INTERVAL = 1u << 8,
    CTL_CONFIG_BAD_INTEGRATION_INTERVAL = 1u << 9,
    CTL_CONFIG_BAD_INPUT_HOLD = 1u << 10,
    CTL_CONFIG_BAD_MAX_AGE = 1u << 11,
    CTL_CONFIG_BAD_FALLBACK = 1u << 12
} ctl_config_problem;

void ctl_config_defaults(ctl_config *config);
uint32_t ctl_config_validate(const ctl_config *config);
double ctl_config_energy_limit_wh(const ctl_config *config);

/* tests/control/check.h */
void check_true(const char *file, int line, bool condition, const char *text);
void check_double(const char *file, int line, double actual, double expected);
void check_uint(const char *file, int line, uint32_t actual, uint32_t expected);
void check_text(const char *file, int line, const char *actual, const char *expected);
int check_failures(void);
void check_reset(void);
int check_result(void);
#define CHECK_TRUE(condition) check_true(__FILE__, __LINE__, (condition), #condition)
#define CHECK_DOUBLE(actual, expected) check_double(__FILE__, __LINE__, (actual), (expected))
#define CHECK_UINT(actual, expected) check_uint(__FILE__, __LINE__, (actual), (expected))
#define CHECK_TEXT(actual, expected) check_text(__FILE__, __LINE__, (actual), (expected))
```

The names:

- Each `ctl_*_name` function returns the name of the Python enum member, which is what a golden trace writes (`docs/control/golden-traces.md`, section 3.3): `"HEATING"`, `"COOLING"`, `"NO_DEMAND"`; `"UNINITIALIZED"`, `"RUNNING"`, `"HOLD"`, `"FAULT"`; `"NORMAL"`, `"SEEDED"`, `"WAITING_FOR_INPUTS"`, `"MASKED"`, `"HELD_STALE_INPUT"`, `"FAULT_TIMEOUT"`, `"FAULT_RECOVERED"`, `"INVALID_TIME"`; `"T_INDOOR"`, `"T_OUTDOOR"`, `"P_PRODUCED"`, `"P_SOLAR"`, `"P_INTERNAL"`.
- A value that is no member of the enum, `CTL_SIGNAL_COUNT` included, gives `"INVALID"`.
- `ctl_signal_is_required` is true for `T_INDOOR`, `T_OUTDOOR` and `P_PRODUCED`, and false for `P_SOLAR`, `P_INTERNAL` and every value that is no member.

The defaults, set by `ctl_config_defaults`:

- `desired_indoor_c` 22.0, `transmission_w_per_k` 155.0, `adjustment_w_per_k` 805.0, `hysteresis_wh` 1500.0, `trim_limit_enabled` true, `trim_limit_k` 3.0, `solar_gain_factor` 0.02, `energy_clamp_factor` 2.0, `max_step_s` 300.0, `tick_interval_s` 60.0, `integration_interval_s` 10.0, `input_hold_s` 900.0.
- `max_age_s` is 600.0 for the four sensed signals and `INFINITY` for `P_INTERNAL`.
- `fallback_w` is 550.0 for `P_INTERNAL` and 0.0 for the other four.
- The defaults are valid: `ctl_config_validate` returns 0 for them.

The validation: `ctl_config_validate` returns 0 for a valid configuration, and otherwise the bits of every field that breaks a rule, all in one call. A field that is NaN or infinite sets its bit, with the one exception named for `max_age_s`.

- `desired_indoor_c`: from 5.0 to 30.0, both included.
- `transmission_w_per_k`: greater than 0.
- `adjustment_w_per_k`: 0 or greater.
- `hysteresis_wh`: greater than 0.
- `trim_limit_k`: greater than 0 when `trim_limit_enabled` is true; not checked at all when it is false.
- `solar_gain_factor`: 0 or greater.
- `energy_clamp_factor`: 1.0 or greater.
- `max_step_s`: greater than 0.
- `tick_interval_s`: greater than 0, and at most half of `max_step_s`.
- `integration_interval_s`: greater than 0, and at most `tick_interval_s`.
- `input_hold_s`: 0 or greater.
- `max_age_s`: every entry greater than 0 and not NaN. The entry of a required signal must be finite; the entry of an optional signal may be `INFINITY`. One bad entry sets the bit.
- `fallback_w`: every entry finite. One bad entry sets the bit.

`ctl_config_energy_limit_wh` returns `energy_clamp_factor * hysteresis_wh`.

Differences from the Python reference, all decided:

- The numbers are `double`, as in Python, and every expression keeps the operand order of the Python source: the golden traces are compared bit for bit.
- `trim_limit_k = None` (`none` in a trace) becomes `trim_limit_enabled = false`.
- The two mappings become arrays indexed by `ctl_signal`. A missing key cannot happen.
- `validate` returns bits, not texts.
- `state_max_age_s` and `persist_interval_s` do not exist: the board stores no state.
- No function takes a null pointer; the caller guarantees it, and no function checks it.

The check helper: each `check_*` function compares, and on a difference it prints one line with the file, the line and both values to standard output and counts the failure. `check_double` passes when the two values are equal as doubles, or when both are NaN; `-0.0` equals `0.0`. It has no tolerance: the core is compared exactly. `check_text` compares with `strcmp`. `check_failures` returns the count and `check_reset` sets it to 0. `check_result` returns 0 when no check failed and 1 otherwise; a test program ends with `return check_result();`.

## Acceptance criteria

1. Each of the four `ctl_*_name` functions returns the text named above for every member of its enum, and `"INVALID"` for the value one past the last member and for -1 cast to the enum.
2. `ctl_signal_is_required` returns true for the three required signals, and false for the two optional ones, for `CTL_SIGNAL_COUNT` and for -1 cast to the enum.
3. `ctl_config_defaults` sets every field to the value named above, and `ctl_config_validate` returns 0 for the result.
4. For each range rule of the validation, a table test starts from the defaults, changes the one field, and checks the result on both sides of every limit: the last accepted value gives 0 and the first refused value gives exactly that field's bit. The rows cover `desired_indoor_c` at 5.0, 30.0, just below 5.0 and just above 30.0; 0 and a small positive value for each "greater than 0" field; 0 and a small negative value for each "0 or greater" field; `energy_clamp_factor` at 1.0 and just below; `tick_interval_s` at exactly half of `max_step_s` and just above; `integration_interval_s` at exactly `tick_interval_s` and just above.
5. For each field of type `double`, and for one entry of each array, NaN and positive infinity each set that field's bit, with the exception that `INFINITY` in `max_age_s` of an optional signal gives 0 and in `max_age_s` of a required signal gives `CTL_CONFIG_BAD_MAX_AGE`.
6. With `trim_limit_enabled` false, `trim_limit_k` at 0, at -1 and at NaN each give 0; with it true, each gives `CTL_CONFIG_BAD_TRIM_LIMIT`.
7. A configuration with three bad fields (`hysteresis_wh` 0, `max_step_s` -1 and `fallback_w` of `P_SOLAR` NaN) gives exactly the three bits `CTL_CONFIG_BAD_HYSTERESIS`, `CTL_CONFIG_BAD_MAX_STEP` and `CTL_CONFIG_BAD_FALLBACK`, and `CTL_CONFIG_BAD_TICK_INTERVAL` as the fourth, because 60 s is then more than half of the maximum step.
8. `ctl_config_energy_limit_wh` returns 3000.0 for the defaults and 3750.0 for a clamp factor of 2.5.
9. The check helper counts. `tests/control/test_types.c` starts with this: one failing call of each of the four `check_*` functions prints four lines and makes `check_failures` return 4 and `check_result` return 1; after `check_reset`, four passing calls print nothing and both return 0. `check_double` passes two equal infinities, two NaN values, and `-0.0` against `0.0`; it fails 1.0 against the next double above 1.0, and NaN against 0.0. The test resets the count before its other checks.
10. `make check` stays green.
11. docs: `docs/control/core.md` is new. It says in one paragraph what the core is and that it is a port of the Python reference beside it, then has one section per module of this story: the enums with their names as text, and the configuration with a table of every field (its meaning in one sentence, its default, its rule) and a table of the problem bits. `docs/control/decisions.md` is new and records the differences from the Python reference listed in this story, each with its reason.

## Smoke test

```sh
make lint | tail -1 | sed 's/(.*//'
```

Expect: `lint: green `

```sh
make test | grep '^control:'
```

Expect: `control: 2 test programs passed`

## Journal

1. 2026-11-11 human: created.
2. factory: started doing as heishamon.
3. builder: Built the core's enums and its configuration with validation, the shared check helper and both documents. `make check` is green.

   Paths:
   - Core: `HeishaMon/src/control/core/ctl_types.h` and `ctl_types.c` hold the four enums, their names as text and `ctl_signal_is_required`. `HeishaMon/src/control/core/ctl_config.h` and `ctl_config.c` hold the defaults, the validation as problem bits and the energy limit.
   - Tests: `tests/control/check.h` and `check.c` are the shared helper. `tests/control/test_types.c` covers the helper first (criterion 9), then the names and the required signals. `tests/control/test_config.c` covers the defaults, range limits, timing limits, NaN and infinity, the signal arrays, the trim-limit switch, several problems in one call and the energy limit.
   - Docs: `docs/control/core.md` and `docs/control/decisions.md` are new.

   I wrote the tests first and saw them fail: the headers were missing. Then I wrote the code.

   Decisions where the story left something open (all recorded in `decisions.md`):
   - **The timing fields depend on each other, and that clashes with criterion 4.** Criterion 7 requires a bad `max_step_s` (-1) to refuse the default tick too, because 60 > -0.5. So the bound is compared as Python compares it (`tick > max_step/2`, `integration > tick`), even when the bounding field is itself bad. As a result:
     - `max_step_s` = 0 cannot give only its own bit. It gives MAX_STEP|TICK_INTERVAL, since no positive tick fits under 0.
     - `tick_interval_s` = 0 gives TICK_INTERVAL|INTEGRATION_INTERVAL for the same reason.
     - A small positive `max_step_s` or `tick_interval_s` is valid only if the fields it bounds are made small enough to fit under it.
     - So `test_config.c` has a separate timing table for these four rows instead of single-field rows. Every other row of criterion 4 changes exactly one field and expects exactly one bit.
     - A NaN or infinite bound refuses only its own field, so criterion 5 gets exact single bits.
     - Rejected: skipping the bound check while the bound is bad. It contradicts criterion 7.
     - The reviewer may want the human's word on this reading of criterion 4.
   - **Range rules are written as "finite and in range".** A NaN then fails every rule. `max_age_s` has its own rule: an entry must not be NaN and must be > 0, and must also be finite for a required signal.
   - **Name lookups use tables indexed by the enum value**, with a bounds check on an `int` cast, so -1 gives "INVALID".
   - **`check_double` prints values with `%.17g`** so that a one-bit difference shows; `check_uint` prints hex.
   - **Expected failure output is labelled.** `test_types.c` prints a line before each group of deliberately failing helper calls, so the expected failure lines in `make test` output are labelled. The four-line output of criterion 9 is seen in the output, not captured. C99 cannot portably capture stdout and restore it without adding to the helper's interface, so I rejected that.

   Smoke test:
   - `make lint | tail -1 | sed 's/(.*//'` gives `lint: green ` (matches).
   - `make test | grep '^control:'` gives `control: 2 test programs passed` (matches).

   Noticed, not touched: upstream's Rules harness prints compiler warnings (`rules.cpp:4640` `-Wmaybe-uninitialized`, unused variables in `settimer.cpp`) to stderr during `make test`. They are upstream's files.

   `check`: check: green
4. factory: started review as heishamon.
5. reviewer: I read the whole diff and compared the validation with `docs/control/python-reference/config.py` (lines 119–190). The code does what Python does, in the same order: the `isfinite` check first, then `tick > max_step / 2.0` and `integration > tick` only when the field itself is > 0, and the same `max_age` rule. The operand order of `energy_limit_wh` matches. The core includes only its own headers, `math.h`, `stdbool.h` and `stdint.h`. Every loop is bounded by `CTL_SIGNAL_COUNT`, every name lookup is bounds-checked, and there is no dynamic memory or `float`. The diff changes no test file that `develop` already holds: every non-factory path has status `A`. The factory's `make check` after the builder was green.

   Findings:

   1. **The story contradicts itself, and the human has not agreed to how it was read.** Location: `tests/control/test_config.c` `test_timing_limits`, and criterion 4 in the work item's `## Description`.
      - Criterion 4 asks that `max_step_s` = 0 and `tick_interval_s` = 0 each give "exactly that field's bit". Criterion 7, and the Python, make `max_step_s` ≤ 0 also refuse the default tick.
      - So `max_step_s` = 0 gives MAX_STEP|TICK_INTERVAL, and `tick_interval_s` = 0 gives TICK_INTERVAL|INTEGRATION_INTERVAL.
      - The builder's reading is the only one that fits both criteria. But R5 says the Description is rewritten when the journal changes the task, and it still states the impossible rule.
      - Fix: the builder ends with `question`, asks the human to confirm the reading, and rewrites criterion 4 from the answer. The proposed wording: "a zero `max_step_s` or `tick_interval_s` also sets the bit of the field it bounds; a small positive one is valid with the bounded fields brought under it".
   2. **The test does not check what criterion 9 says about printed output.** Location: `tests/control/test_types.c` `test_check_helper`.
      - Criterion 9 says four failing calls "print four lines" and four passing calls "print nothing". The test checks only the counts. Nothing checks the printed output, and a helper that printed nothing would pass.
      - Fix: put this in the same question to the human. Either they accept that the output is checked by eye in the `make test` log, and criterion 9 is reworded to say so, or they allow a change to the helper's interface that makes the output testable, such as an output `FILE *`.
   3. **`docs/control/core.md`, first paragraph, makes two claims the code does not back.**
      - "the golden traces exported from that reference check it bit for bit": no test of this story reads a trace.
      - "from five measured signals": `P_INTERNAL` is not measured. Its default never goes stale, and the code comment in `ctl_config.c` calls it "usually a static estimate".
      - Fix: say the traces are the measure the port is compared against, and say "five input signals".
   4. **Duplicate checks.** Location: `tests/control/test_config.c` `test_signal_arrays`, the two `CHECK_UINT` lines after the loop.
      - They repeat checks the loop already makes for `T_INDOOR` and `P_SOLAR` with `INFINITY`.
      - Fix: delete them.
6. factory: started doing as heishamon.
7. builder: Round 1. I fixed reviewer findings 3 and 4. Findings 1 and 2 need your decision, so I am asking before I change criteria 4 and 9 in the Description. `make check` is green with the fixes.

   Fixed:
   - Finding 3: in `docs/control/core.md`, the first paragraph now says "five input signals", not "measured". It also says the golden traces are the measure the port is compared against, bit for bit. It no longer claims they already check the port.
   - Finding 4: in `tests/control/test_config.c` `test_signal_arrays`, I deleted the two `CHECK_UINT` lines after the loop. The loop already makes both checks.

   Question 1, on criterion 4 and the timing fields:
   - Criterion 4 asks that `max_step_s` = 0 and `tick_interval_s` = 0 each give only their own bit.
   - Criterion 7, like Python's `config.py`, checks `tick > max_step/2` even when `max_step_s` is itself bad.
   - Under that rule, `max_step_s` = 0 gives MAX_STEP|TICK_INTERVAL, and `tick_interval_s` = 0 gives TICK_INTERVAL|INTEGRATION_INTERVAL.
   - The code and `test_config.c` `test_timing_limits` already work this way. A small positive bound is valid there only when the fields it bounds are brought under it.

   Options:
   - (a) Keep this behaviour and reword criterion 4: "a zero `max_step_s` or `tick_interval_s` also sets the bit of the field it bounds; a small positive one is valid with the bounded fields brought under it". **This is my choice**: it matches Python and criterion 7.
   - (b) Skip a bound check while the bounding field is bad. This departs from Python and breaks criterion 7's fourth bit, so criterion 7 would have to change too.

   Question 2, on criterion 9 and the printed output:
   - The test checks the failure counts, but not that four failing calls print four lines and four passing calls print nothing.

   Options:
   - (a) Accept that the lines are checked by reading the `make test` log. The test labels the deliberately failing calls. Criterion 9 would be reworded to say "the four lines appear in the `make test` output; the test checks the counts". **This is my choice**: it keeps the helper's interface as the story defines it.
   - (b) Add `void check_set_output(FILE *out);` to `check.h`. Then the test can point the helper at a `tmpfile()` and count the lines it wrote, and the Interface section gains that function.

   Once you answer, I will rewrite criteria 4 and 9 from your answer and make the matching code or test change.
8. 2026-11-11 human: Question 1: (a). Keep the behaviour, which matches the Python reference and criterion 7, and reword criterion 4 as you propose: a zero `max_step_s` or `tick_interval_s` also sets the bit of the field it bounds, and a small positive one is valid with the bounded fields brought under it. The contradiction was in the story, not in your reading.

   Question 2: (a). The printed lines are read in the `make test` log; the test checks the counts. Reword criterion 9 to say so, and keep the helper's interface as it is.

   — Kumpel, for the human who owns this project
   <!-- kumpel -->
9. factory: started doing as heishamon.
10. builder: Round 1, after the human's answer. I made both answers part of the Description and recorded the second one as a decision. No code or test changed in this run, because the code and tests already worked the way the human chose. `make check` is green.

    Changed:
    - Criterion 4 in the work item's `## Description` now names the exception, as the human chose in option (a). A zero `max_step_s` or `tick_interval_s` also sets the bit of the field it bounds (`CTL_CONFIG_BAD_TICK_INTERVAL` or `CTL_CONFIG_BAD_INTEGRATION_INTERVAL`). A small positive one is valid once the bounded fields are brought under it. `tests/control/test_config.c` `test_timing_limits` already checks exactly this, and `docs/control/decisions.md` already had the matching entry, "A bound on a timing field is compared as in Python…".
    - Criterion 9 in the Description now says the test checks the counts, as the human chose in option (a). The four printed lines are read in the `make test` output under the line that labels them as expected failures, and that is also where you see that passing calls print nothing. The helper's interface is unchanged.
    - `docs/control/decisions.md` has a new entry, "The check helper's printed lines are read in the test log, not tested", with its reason. The rejected option is `check_set_output(FILE *)`.

    Earlier fixes, unchanged since the last round: findings 3 (`docs/control/core.md` wording) and 4 (duplicate checks deleted in `test_config.c`).

    Smoke test:
    - `make lint | tail -1 | sed 's/(.*//'` gives `lint: green ` (matches).
    - `make test | grep '^control:'` gives `control: 2 test programs passed` (matches).

    Noticed, not touched: upstream's Rules harness still prints compiler warnings to stderr during `make test`. They are upstream's files.

    The factory left out changes outside the write paths: factory/features/F0001-control-core/in-progress/F0001-S0001-types-and-configuration.md.

    `check`: check: green
