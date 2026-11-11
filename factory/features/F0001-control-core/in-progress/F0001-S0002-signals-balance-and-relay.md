---
stage: doing
blocked: null
round: 1
attempts: 0
crashes: 0
pull_request: 4
comments_read: []
handed_over: null
claim:
  instance: heishamon
  stage: doing
  started_at: '2026-11-11T19:55:46.316324Z'
  lease_duration: 60
journal:
- number: 1
  speaker: human
  kind: created
  at: '2026-11-11T19:51:27.905888Z'
  stage: doing
- number: 2
  speaker: factory
  kind: started
  at: '2026-11-11T19:51:27.911722Z'
  stage: doing
- number: 3
  speaker: builder
  kind: result
  at: '2026-11-11T19:54:19.964699Z'
  stage: doing
  run: 09c9e8a035f9412da242b7142c5f4d0d
- number: 4
  speaker: factory
  kind: started
  at: '2026-11-11T19:54:26.963793Z'
  stage: review
- number: 5
  speaker: reviewer
  kind: result
  at: '2026-11-11T19:55:39.713293Z'
  stage: review
  run: 1322e207c8424a95b72009b296a1617d
- number: 6
  speaker: factory
  kind: started
  at: '2026-11-11T19:55:46.316324Z'
  stage: doing
---

# The signal table, the energy balance and the relay

## Description

The control core gets the three modules the controller is built from, each a set of functions over values: the signal table that holds the last reading of each of the five signals and resolves them to one instant; the energy balance with its ledger; and the tri-state relay with its anti-windup clamp. The owner needs them as separate, tested parts before the controller of the next story combines them.

The modules are a port of `docs/control/python-reference/signals.py`, `balance.py` and `hysteresis.py`, and of the types `Inputs`, `Balance` and `EnergyLedger` in `types.py`. `docs/control/reference.md` sections 2.3, 2.4 and 2.6 describe them. Every expression keeps the operand order of the Python source, because the golden traces are compared bit for bit. Where this story and the Python differ, this story wins; the differences are listed under *Interface*.

Left to later stories: the controller state, `advance`, `observe`, `set_allowed`, `evaluate`, the decision type, and the trace runner. The story changes nothing in `ctl_types` and `ctl_config`.

## Interface

New files under `HeishaMon/src/control/core/`: `ctl_signals.h`, `ctl_signals.c`, `ctl_balance.h`, `ctl_balance.c`, `ctl_relay.h`, `ctl_relay.c`. Test files: `tests/control/test_signals.c`, `tests/control/test_balance.c`, `tests/control/test_relay.c`. They use the check helper `tests/control/check.h` as it is.

```c
/* ctl_signals.h */
#define CTL_SIGNAL_BIT(signal) ((uint8_t)(1u << (signal)))

typedef struct {
    bool observed[CTL_SIGNAL_COUNT];
    double value[CTL_SIGNAL_COUNT];
    double t[CTL_SIGNAL_COUNT];
} ctl_signal_table;

typedef struct {
    double t_indoor_c;
    double t_outdoor_c;
    double p_produced_w;
    double p_solar_w;
    double p_internal_w;
    uint8_t substituted; /* CTL_SIGNAL_BIT of each optional signal that fell back */
} ctl_inputs;

void ctl_signals_init(ctl_signal_table *table);
void ctl_signals_set(ctl_signal_table *table, ctl_signal signal, double value, double t);
double ctl_signals_age(const ctl_signal_table *table, ctl_signal signal, double at);
bool ctl_signals_is_fresh(const ctl_signal_table *table, ctl_signal signal, double at, const ctl_config *config);
uint8_t ctl_signals_unhealthy(const ctl_signal_table *table, double at, const ctl_config *config);
uint8_t ctl_signals_stale_optional(const ctl_signal_table *table, double at, const ctl_config *config);
uint8_t ctl_signals_observed(const ctl_signal_table *table);
bool ctl_signals_resolve(const ctl_signal_table *table, double at, const ctl_config *config, ctl_inputs *inputs);

/* ctl_balance.h */
typedef struct {
    double p_transmission_w;
    double p_model_w;
    double p_trim_w;
    double p_net_w;
} ctl_balance;

typedef struct {
    double produced_wh;
    double solar_wh;
    double internal_wh;
    double transmission_wh;
    double trim_wh;
} ctl_ledger;

void ctl_energy_balance(const ctl_inputs *inputs, const ctl_config *config, ctl_balance *balance);
void ctl_ledger_init(ctl_ledger *ledger);
double ctl_ledger_total_wh(const ctl_ledger *ledger);
void ctl_ledger_accumulate(ctl_ledger *ledger, const ctl_inputs *inputs, const ctl_balance *balance, double hours);

/* ctl_relay.h */
#define CTL_DEMAND_BIT(demand) ((uint8_t)(1u << (demand)))
#define CTL_DEMAND_ALL ((uint8_t)(CTL_DEMAND_BIT(CTL_DEMAND_HEATING) | CTL_DEMAND_BIT(CTL_DEMAND_COOLING) | CTL_DEMAND_BIT(CTL_DEMAND_NONE)))

ctl_demand ctl_relay(double energy_wh, ctl_demand previous, double hysteresis_wh);
void ctl_clamp_bounds(uint8_t allowed, const ctl_config *config, double *low, double *high);
double ctl_clamp(double value, double low, double high);
```

The signal table:

- `ctl_signals_init` leaves no signal observed.
- `ctl_signals_set` stores the value and its instant for the signal and marks it observed. It checks nothing: the newest call wins, whatever its instant.
- `ctl_signals_age` returns `at - t` of the signal, which is negative when `at` lies before the reading, and `INFINITY` for a signal never observed.
- `ctl_signals_is_fresh` is false for a signal never observed, and otherwise `(at - t) <= max_age_s[signal]`. An age equal to the maximum is fresh. A NaN `at` is not fresh.
- `ctl_signals_unhealthy` returns the bits of the required signals that are not fresh at `at`; `ctl_signals_stale_optional` the bits of the optional signals that are not fresh; `ctl_signals_observed` the bits of every signal observed, fresh or not.
- `ctl_signals_resolve` returns false when any required signal is not fresh, and then it does not write `inputs`. Otherwise it returns true and fills `inputs`: each required signal's held value; each optional signal's held value when it is fresh, and `fallback_w[signal]` with its bit in `substituted` when it is not.
- The staleness test is written `!(age <= max_age)`, not `age > max_age`: the two differ for NaN.

The balance, in this operand order:

- `p_transmission_w = transmission_w_per_k * (t_outdoor_c - t_indoor_c)`.
- `p_model_w = p_produced_w + p_solar_w + p_internal_w + p_transmission_w`, summed from left to right.
- The deviation is `t_indoor_c - desired_indoor_c`. With `trim_limit_enabled` it is clamped to `-trim_limit_k` below and `trim_limit_k` above; a deviation equal to a limit is unchanged. `p_trim_w = adjustment_w_per_k * deviation`.
- `p_net_w = p_model_w + p_trim_w`.

The ledger:

- `ctl_ledger_init` sets the five terms to 0.
- `ctl_ledger_accumulate` adds to each term its power times `hours`: `p_produced_w`, `p_solar_w` and `p_internal_w` from the inputs, `p_transmission_w` and `p_trim_w` from the balance.
- `ctl_ledger_total_wh` returns `produced_wh + solar_wh + internal_wh + transmission_wh + trim_wh`, summed from left to right.

The relay and the clamp:

- From `HEATING`: `NONE` when `energy_wh >= 0.0`, otherwise `HEATING`. From `COOLING`: `NONE` when `energy_wh <= 0.0`, otherwise `COOLING`. Neither goes straight to the other direction.
- From `NONE`, or from a value that is no member of the enum: `HEATING` when `energy_wh <= -hysteresis_wh`, else `COOLING` when `energy_wh >= hysteresis_wh`, else `NONE`.
- A NaN energy keeps `HEATING` and `COOLING` as they are and gives `NONE` from `NONE`.
- `ctl_clamp_bounds`: the limit is `ctl_config_energy_limit_wh`. `*low` is `-hysteresis_wh` when `allowed` lacks the bit of `HEATING`, and minus the limit when it has it. `*high` is `hysteresis_wh` when `allowed` lacks the bit of `COOLING`, and the limit when it has it. The bit of `NONE` plays no part.
- `ctl_clamp` returns `low` when `value < low`, `high` when `value > high`, and `value` otherwise, so a NaN passes through.

Differences from the Python reference, all decided:

- The table is a struct the caller owns and `ctl_signals_set` changes in place; Python makes a new table.
- A set of signals or of demands is a bit mask in a `uint8_t`.
- `resolve` returns false where Python returns `None`.
- `Held` has no type of its own: the table's three arrays hold it.
- The ledger is changed in place, and it has no `as_dict`.
- No function takes a null pointer or a signal outside the enum; the caller guarantees both, and no function checks.

## Acceptance criteria

1. A fresh table has no signal observed: `ctl_signals_observed` returns 0, `ctl_signals_age` returns `INFINITY` for each of the five signals, and `ctl_signals_is_fresh` is false for each with the default configuration.
2. After `ctl_signals_set(T_INDOOR, 21.5, 100.0)`, the age at 160.0 is 60.0 and at 40.0 is -60.0, `ctl_signals_observed` returns exactly the bit of `T_INDOOR`, and a second set of the same signal with value 20.0 at instant 50.0 replaces both the value and the instant.
3. Freshness at the limit, as rows of one table test with the default configuration and a reading at instant 100.0: `T_INDOOR` is fresh at 700.0 (age 600.0), not fresh at 700.001, fresh at 40.0 (negative age), and not fresh at NaN; `P_INTERNAL`, whose maximum age is `INFINITY`, is fresh at 1e12.
4. `ctl_signals_unhealthy` and `ctl_signals_stale_optional`: with only `T_INDOOR` and `P_SOLAR` set at 100.0 and asked at 200.0, the first returns the bits of `T_OUTDOOR` and `P_PRODUCED`, and the second the bit of `P_INTERNAL`. Asked at 800.0, the first returns all three required bits and the second both optional bits.
5. `ctl_signals_resolve` with the default configuration, asked at 200.0:
   - With the three required signals set at 100.0 (21.0, 5.0, 3000.0) and no optional signal, it returns true with those three values, `p_solar_w` 0.0, `p_internal_w` 550.0, and `substituted` holding both optional bits.
   - With `P_SOLAR` 120.0 and `P_INTERNAL` 400.0 also set at 100.0, it returns true with those values and `substituted` 0.
   - With `P_SOLAR` set at 100.0 and asked at 800.0 while the required signals were set at 750.0, `p_solar_w` is 0.0 and `substituted` holds the bit of `P_SOLAR` and of `P_INTERNAL`.
   - With one required signal missing, with one required signal stale, and with `at` NaN, it returns false and leaves an `inputs` struct that was filled with a marker value unchanged in every field.
6. `ctl_energy_balance` with the default configuration and the inputs indoor 21.0, outdoor 5.0, produced 3000.0, solar 100.0, internal 550.0 gives `p_transmission_w` -2480.0, `p_model_w` 1170.0, `p_trim_w` -805.0 and `p_net_w` 365.0.
7. The trim clamp, as rows of one table test with the same inputs except the indoor temperature: 19.0 (deviation -3.0, the limit itself) gives `p_trim_w` -2415.0; 17.0 gives -2415.0; 25.0 gives 2415.0; 26.0 gives 2415.0; and with `trim_limit_enabled` false, 17.0 gives -4025.0 and 26.0 gives 3220.0.
8. The ledger: after `ctl_ledger_init` the total is 0.0. One `ctl_ledger_accumulate` with the inputs and the balance of criterion 6 and 0.5 hours gives produced 1500.0, solar 50.0, internal 275.0, transmission -1240.0, trim -402.5 and a total of 182.5; a second one with the same arguments doubles every term.
9. `ctl_relay` with a hysteresis of 1500.0, as rows of one table test:
   - From `NONE`: -1500.0 gives `HEATING`; the next double above -1500.0 gives `NONE`; 0.0 gives `NONE`; the next double below 1500.0 gives `NONE`; 1500.0 gives `COOLING`; NaN gives `NONE`.
   - From `HEATING`: the next double below 0.0 gives `HEATING`; 0.0 gives `NONE`; 2000.0 gives `NONE`; NaN gives `HEATING`.
   - From `COOLING`: the next double above 0.0 gives `COOLING`; 0.0 gives `NONE`; -2000.0 gives `NONE`; NaN gives `COOLING`.
10. `ctl_clamp_bounds` with the default configuration, as rows of one table test: all three demands allowed gives -3000.0 and 3000.0; without `HEATING`, -1500.0 and 3000.0; without `COOLING`, -3000.0 and 1500.0; with `NONE` alone, -1500.0 and 1500.0; with `HEATING` and `COOLING` but without `NONE`, -3000.0 and 3000.0.
11. `ctl_clamp` with the bounds -1.0 and 2.0: -1.5 gives -1.0, -1.0 gives -1.0, 0.5 gives 0.5, 2.0 gives 2.0, 2.5 gives 2.0, and NaN gives NaN.
12. `make check` stays green.
13. docs: `docs/control/core.md` gets one section per module of this story, after the sections it has: the signal table (what it holds, what fresh means, what `resolve` returns and when it refuses), the balance (the four formulas in their operand order, and the ledger) and the relay (its transitions as a table, and the clamp bounds as a table). `docs/control/decisions.md` records the differences from the Python reference listed in this story, each with its reason.

## Smoke test

```sh
make lint | tail -1 | sed 's/(.*//'
```

Expect: `lint: green `

```sh
make test | grep '^control:'
```

Expect: `control: 5 test programs passed`

## Journal

1. 2026-11-11 human: created.
2. factory: started doing as heishamon.
3. builder: I built the signal table, the energy balance with its ledger, and the relay with its clamp, following the story's interface. I wrote the tests first and they failed because the headers were missing. Then I wrote the code until `make check` was green (lint green, 5 control test programs passed, `check: green`).

   Code, in `HeishaMon/src/control/core/`:
   - `ctl_signals.h` and `ctl_signals.c`
   - `ctl_balance.h` and `ctl_balance.c`
   - `ctl_relay.h` and `ctl_relay.c`

   Tests, in `tests/control/`:
   - `test_signals.c` covers criteria 1–5. The NaN-`at` case and the refusal cases each check every field of an `inputs` struct filled with a marker value.
   - `test_balance.c` covers criteria 6–8. The trim clamp is one table test.
   - `test_relay.c` covers criteria 9–11 as table tests. Neighbouring doubles come from `nextafter`, and demands are compared by their `ctl_demand_name` text.

   All three use `check.h` unchanged.

   Docs:
   - `docs/control/core.md` has three new sections after the existing ones:
     - the signal table: what it holds, what fresh means, and what `resolve` returns and when it refuses;
     - the balance: the formulas as a table in their operand order, and the ledger;
     - the relay: a transition table and a clamp-bounds table.
   - `docs/control/decisions.md` has one entry for each of the six listed differences from the Python reference, each with its reason. It also has one entry for a decision of my own, below.

   Decisions where the story left something open:
   - **`ctl_signals_init` sets `value` and `t` to 0.0 as well as clearing `observed`.** The struct is then fully defined and can be copied or compared. I rejected leaving them unset. This is recorded in `decisions.md`.
   - **Staleness is the negation of `ctl_signals_is_fresh`.** One static helper, `not_fresh`, filters by `ctl_signal_is_required` and serves both `unhealthy` and `stale_optional`. `resolve` uses those two masks, so the `!(age <= max_age)` behaviour for NaN lives in one place. I rejected a separate `age > max_age` test inside `resolve`.
   - **The trim clamp is a static function, `trim_deviation`, using Python's comparison order** (`< -limit`, then `> limit`), so a NaN deviation passes through unchanged.
   - **A `previous` value that is not in the enum falls through to the `NONE` branch of `ctl_relay`**, as the story says. No explicit check is needed.

   Smoke test:
   - `make lint | tail -1 | sed 's/(.*//'` printed `lint: green `, which matches.
   - `make test | grep '^control:'` printed `control: 5 test programs passed`, which matches.

   Noticed but not touched:
   - Upstream's rules harness prints compiler warnings on stderr during `make test`, from `rules.cpp` and `settimer.cpp`. Those are upstream files.
   - `ctl_inputs` lives in `ctl_signals.h` as the interface puts it, so `ctl_balance.h` includes `ctl_signals.h`.

   Open: nothing.

   `check`: check: green
4. factory: started review as heishamon.
5. reviewer: Reviewed `git diff develop...HEAD` (factory files excluded) against the story, the Python reference (`signals.py`, `balance.py`, `hysteresis.py`, `types.py`), `ctl_types.h`, `ctl_config.[ch]` and `check.h`.

   All of these check out:
   - Operand order matches Python everywhere: the trim clamp comparison order, the ledger `+=` with `power * hours`, and the left-to-right sums.
   - Staleness is `!(age <= max)`, done once through `ctl_signals_is_fresh`.
   - Core includes are only from the allowed set. Public names have the `ctl_`/`CTL_` prefix and helpers are `static`.
   - There is no dynamic memory. Every loop is bounded by `CTL_SIGNAL_COUNT`.
   - Each header function has a comment saying what it returns and what it refuses.
   - The claims in `core.md` match the code, including the limit of 3000 = factor × hysteresis. `decisions.md` has the six listed differences plus the builder's decision about `init`.
   - Every test file in the diff is new (`A`), so no earlier test was changed. The journal reports `make check` green.

   Findings. Both are rules the Interface states that have no criterion and no test, so the human should also see them as gaps in the story:

   1. `tests/control/test_relay.c` (`test_relay`): there is no test of the Interface rule "from a value that is no member of the enum" (`ctl_relay.h` documents it too). A `previous` outside the enum that is not handled as `NONE` would still pass every row. Fix: add rows with `previous` set to `(ctl_demand)3`: -1500.0 → `HEATING`, 0.0 → `NONE`, 1500.0 → `COOLING`. Gap in the story: criterion 9 has no row for a non-member `previous`.
   2. `tests/control/test_signals.c` (the `resolve` tests): the per-signal fallback rule ("each optional signal's held value when it is fresh, and `fallback_w` … when it is not") is only tested with both optional signals fresh or both stale. A wrong `optional_value` that tests `stale != 0` instead of the signal's own bit would pass all three resolve tests. Fix: add a case with one optional signal fresh and the other stale. Example: `P_SOLAR` 120.0 at 100.0, `P_INTERNAL` 400.0 and the required signals at 750.0, asked at 800.0. Expect `p_solar_w` 0.0, `p_internal_w` 400.0, and `substituted` holding exactly the bit of `P_SOLAR`. Gap in the story: criterion 5 has no case with mixed optional signals.
6. factory: started doing as heishamon.
