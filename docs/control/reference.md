# `heatnrgx.core` — API Reference

Lookup manual for engineers building against the decision core: writing an adapter, embedding the
controller in a runtime, or calling it from a replay tool. Everything the core exports is listed
here with its exact signature, units, ranges and failure behaviour.

This document says **what** the core does. `core.md` describes its C port, and `decisions.md`
records why the fork's choices were made. `test-harness.md` covers the simulator.

Python 3.10+ (`dataclass(slots=True)`). No third-party dependencies. Verified against the code on
Python 3.14.

---

## 1. Orientation

The core turns a stream of timestamped scalar readings into a tri-state demand — `HEATING`,
`COOLING`, or `NO_DEMAND` — plus a `Decision` record that explains itself. It does no I/O, reads no
clock, uses no randomness, spawns nothing, logs nothing and raises nothing on any float input.

**Mental model.** The house is a thermal battery. Five signals — indoor and outdoor temperature,
heat produced, solar gain, internal gain — are combined into an instantaneous power balance
`p_net` in watts: negative means the house is losing charge. `p_net` is integrated over elapsed
time into one scalar `E`, the *energy deviation* in Wh. A tri-state relay reads `E`: engage heating
at `E ≤ -H`, engage cooling at `E ≥ +H`, release either at `E = 0`. Everything else in the core
exists to make that integral safe when readings arrive late, out of order, or not at all.

```python
from heatnrgx.core import Config, Controller, Demand, Signal

cfg = Config()
assert cfg.validate() == []          # never raises; empty list means usable

ctrl = Controller(cfg)

t = 0.0                              # monotonic seconds, supplied by you
while t <= 3600.0:
    ctrl.observe(Signal.T_INDOOR, 21.5, t)      # degC
    ctrl.observe(Signal.T_OUTDOOR, 0.0, t)      # degC
    ctrl.observe(Signal.P_PRODUCED, 0.0, t)     # W, thermal, space heating only
    decision = ctrl.evaluate(t)                 # decide on the 60 s heartbeat
    if decision.demand is Demand.HEATING:
        break
    t += 60.0

print(decision.demand.name, decision.reason.value, f"{decision.energy_wh:.1f}")
print(decision.describe())
```

```
HEATING normal -1539.4
t=1740.0 HEATING (normal/running) E=-1539.4Wh of +-1500 net=-3185W dt=0.0s sub=['P_INTERNAL', 'P_SOLAR']
```

Solar and internal gain were never published, so both fall back to their configured values and the
decision reports `sub=[...]`. The loop still runs; see §7.

The same thing without the facade, threading state explicitly, plus a persistence round trip:

```python
from heatnrgx.core import (
    Config, Reading, Signal, advance_to, evaluate, observe_all, restore, to_snapshot,
)

cfg = Config()
state = restore(None, now_wall=1_700_000_000.0, cfg=cfg)   # cold start

state = observe_all(state, [
    Reading(Signal.T_INDOOR, 21.5, 0.0),
    Reading(Signal.T_OUTDOOR, 0.0, 0.0),
    Reading(Signal.P_PRODUCED, 0.0, 0.0),
], cfg)
state, decision = evaluate(state, 0.0, cfg)                # -> reason SEEDED

state, _dt, _integrated = advance_to(state, 10.0, cfg)     # fast tick, no decision
state, decision = evaluate(state, 60.0, cfg)               # decision tick

snap = to_snapshot(state, now_wall=1_700_000_060.0)
back = restore(snap, now_wall=1_700_000_120.0, cfg=cfg)    # E and demand survive
```

```
seed : t=0.0 NO_DEMAND (seeded/running) E=+0.0Wh of +-1500 net=-3185W dt=0.0s sub=['P_INTERNAL', 'P_SOLAR']
tick : t=60.0 NO_DEMAND (normal/running) E=-53.1Wh of +-1500 net=-3185W dt=50.0s sub=['P_INTERNAL', 'P_SOLAR']
snap : StateSnapshot(energy_wh=-53.08333333333333, demand=<Demand.NO_DEMAND: 'no_demand'>, saved_at=1700000060.0, version=1)
back : E=-53.0833 demand=NO_DEMAND phase=UNINITIALIZED t_last=None
```

Note `dt=50.0s` on the second line: `advance_to(10.0)` already closed `[0, 10)`, so the `evaluate`
at `t=60` only closed `[10, 60)`. Per-call `dt_s` is not the tick period. See §4.

---

## 2. Public symbols

Everything in `heatnrgx.core.__all__`, in dependency order. Nothing outside this list is API.

### 2.1 Enums

#### `Demand`

The output. Tri-state, no modulation ([A-15]).

| Member | `.value` | Meaning |
|---|---|---|
| `Demand.HEATING` | `"heating"` | Deliver heat into the house |
| `Demand.COOLING` | `"cooling"` | Extract heat from the house |
| `Demand.NO_DEMAND` | `"no_demand"` | Neither; the neutral and always-permitted state |

#### `Phase`

Lifecycle state of the controller. Read it to decide how much to trust the demand.

| Member | `.value` | Meaning | Demand emitted |
|---|---|---|---|
| `Phase.UNINITIALIZED` | `"uninitialized"` | Not all required signals have been seen yet | `NO_DEMAND` |
| `Phase.RUNNING` | `"running"` | Inputs healthy, integrating, deciding normally | whatever the relay says |
| `Phase.HOLD` | `"hold"` | A required input is stale; last demand coasted, integral frozen | previous demand |
| `Phase.FAULT` | `"fault"` | Stale beyond `input_hold_s`; output forced off | `NO_DEMAND` |

Transitions: `UNINITIALIZED → RUNNING` on the first successful resolve; `RUNNING → HOLD → FAULT`
as staleness persists; `HOLD → RUNNING` and `FAULT → RUNNING` on recovery. There is no
`UNINITIALIZED → HOLD` edge — the hold timer needs a healthy instant to measure from.

#### `Reason`

Why the decision came out the way it did. **Diagnostics only — never branch control logic on it.**
Map it to log levels:

| Member | `.value` | Means | Suggested level | Expected in steady state |
|---|---|---|---|---|
| `Reason.NORMAL` | `"normal"` | Relay decided on a healthy balance | `DEBUG` | yes, almost always |
| `Reason.SEEDED` | `"seeded"` | First evaluate after all required signals resolved; clock reference established, nothing integrated | `INFO` | once per start |
| `Reason.WAITING_FOR_INPUTS` | `"waiting_for_inputs"` | Still `UNINITIALIZED`; at least one required signal never seen | `INFO` at start, `WARN` after ~1 min | no |
| `Reason.MASKED` | `"masked"` | The relay wanted a direction the supervisor forbids; forced to `NO_DEMAND` | `INFO` | only while a mask is set |
| `Reason.HELD_STALE_INPUT` | `"held_stale_input"` | A required signal went stale; coasting on the last demand | `WARN` | brief and rare |
| `Reason.FAULT_TIMEOUT` | `"fault_timeout"` | Staleness outlasted `input_hold_s`; output forced off | `ERROR` | no |
| `Reason.FAULT_RECOVERED` | `"fault_recovered"` | Inputs came back after a `FAULT`; `E` reset to 0, demand reset to `NO_DEMAND` ([A-11]) | `WARN` | no |
| `Reason.INVALID_TIME` | `"invalid_time"` | The caller passed a non-finite instant. The call was ignored and **no state changed**; the decision echoes the current demand and phase | `ERROR` | no |

`SEEDED` and `FAULT_RECOVERED` are one-shot: exactly one decision carries each, per occurrence.

#### `Signal`

The five measured inputs. Each arrives independently on its own cadence.

| Member | `.value` | Unit | Required | Sign convention |
|---|---|---|---|---|
| `Signal.T_INDOOR` | `"t_indoor_c"` | °C | yes | — |
| `Signal.T_OUTDOOR` | `"t_outdoor_c"` | °C | yes | — |
| `Signal.P_PRODUCED` | `"p_produced_w"` | W | yes | `+` thermal power **into** the house, `−` extracted |
| `Signal.P_SOLAR` | `"p_solar_w"` | W | no | `+` gain into the house |
| `Signal.P_INTERNAL` | `"p_internal_w"` | W | no | `+` gain into the house |

`P_PRODUCED` is **thermal** output, **space heating only**: DHW and defrost must be reported as
`0 W`, and electrical input must not be substituted for thermal output. Getting this wrong silently
biases the whole loop — see [A-3].

### 2.2 Constants

| Symbol | Type | Value |
|---|---|---|
| `ALL_DEMANDS` | `frozenset[Demand]` | all three members |
| `REQUIRED_SIGNALS` | `tuple[Signal, ...]` | `(T_INDOOR, T_OUTDOOR, P_PRODUCED)` |
| `OPTIONAL_SIGNALS` | `tuple[Signal, ...]` | `(P_SOLAR, P_INTERNAL)` |
| `SNAPSHOT_VERSION` | `int` | `1` |
| `DEFAULT_MAX_AGE_S` | `Mapping[Signal, float]` | `600.0` for the four sensed signals, `inf` for `P_INTERNAL` |
| `DEFAULT_FALLBACK_W` | `Mapping[Signal, float]` | `{P_SOLAR: 0.0, P_INTERNAL: 550.0}` |

Both mappings are `MappingProxyType` — read-only, safe to share, and the defaults for
`Config.max_age_s` / `Config.fallback_w`.

### 2.3 Value types

All frozen `dataclass(slots=True)`. Immutable, comparable by value, and free of behaviour beyond
trivial derivation. Construct them positionally or by keyword.

#### `Reading(signal: Signal, value: float, t: float)`

One measurement of one signal at one instant.

| Field | Unit | Notes |
|---|---|---|
| `signal` | — | which of the five |
| `value` | °C or W per `Signal` | must be finite or the reading is dropped |
| `t` | monotonic s | **sample time, not arrival time.** Transport latency must not be folded in |

#### `Held(value: float, t: float)`

The last observed value of one signal, and when it became true. Returned by `SignalTable.held`.

#### `Inputs(t_indoor_c, t_outdoor_c, p_produced_w, p_solar_w, p_internal_w, substituted=frozenset())`

All five signals resolved to a single instant by zero-order hold. `substituted:
frozenset[Signal]` lists the optional signals that fell back rather than being measured.

#### `Balance(p_transmission_w, p_model_w, p_trim_w, p_net_w)`

The instantaneous power balance, all in watts.

| Field | Definition |
|---|---|
| `p_transmission_w` | `k_trans * (T_out - T_in)`; `>0` = house gains from outside |
| `p_model_w` | `p_produced + p_solar + p_internal + p_transmission` — the prediction |
| `p_trim_w` | `a_trim * clamp(T_in - T_set, ±L)` — the correction; `<0` when too cold |
| `p_net_w` | `p_model_w + p_trim_w`; `<0` = deficit, `>0` = surplus |

#### `EnergyLedger(produced_wh=0, solar_wh=0, internal_wh=0, transmission_wh=0, trim_wh=0)`

Lifetime Wh contributed by each term. This is the instrumentation you use to identify
`transmission_w_per_k` and the gain factors from field data.

| Member | Signature | Notes |
|---|---|---|
| `total_wh` | `property -> float` | sum of the five terms, **before** the anti-windup clamp |
| `accumulate(inputs, balance, hours)` | `(Inputs, Balance, float) -> EnergyLedger` | returns a new ledger; does not mutate |
| `as_dict()` | `() -> dict[str, float]` | keys `produced`, `solar`, `internal`, `transmission`, `trim` (no `_wh` suffix) |

A persistent gap between `total_wh` and `Decision.energy_wh` is exactly the energy the clamp has
discarded. See §6.

#### `Decision(...)`

The output plus everything needed to explain it. Fields in §6.

#### `StateSnapshot(energy_wh: float, demand: Demand, saved_at: float, version: int = SNAPSHOT_VERSION)`

The only thing that survives a restart. `saved_at` is **wall** clock seconds (unix time), not
monotonic — monotonic time does not survive a process restart ([A-12]).

| Member | Signature | Returns |
|---|---|---|
| `is_valid()` | `() -> bool` | `version == SNAPSHOT_VERSION` and both `energy_wh` and `saved_at` finite |

Serialise the four fields yourself; the core has no codec. Write atomically (temp file + rename) —
a torn snapshot must read back as absent, not as garbage.

### 2.4 `SignalTable`

Immutable `Signal -> Held` map, copy-on-write, at most five entries. Held internally on
`ControllerState.signals`; you rarely construct one, but you may read it for diagnostics.

| Signature | Returns | Notes |
|---|---|---|
| `SignalTable()` | — | empty table |
| `.with_reading(signal: Signal, value: float, t: float) -> SignalTable` | new table | no validation; `observe` does that |
| `.held(signal: Signal) -> Held \| None` | last value | `None` if never observed |
| `.age(signal: Signal, at: float) -> float` | seconds | `+inf` if never observed; **negative** if `at` precedes the reading |
| `.is_fresh(signal: Signal, at: float, cfg: Config) -> bool` | | `False` if never observed; `age <= cfg.max_age(signal)` |
| `.unhealthy(at: float, cfg: Config) -> frozenset[Signal]` | required signals that are missing or stale | |
| `.stale_optional(at: float, cfg: Config) -> frozenset[Signal]` | optional signals that are missing or stale | |
| `.observed() -> frozenset[Signal]` | every signal with a held value | ignores staleness |

### 2.5 `ControllerState`

Everything the loop remembers. Frozen; threaded through the pure functions, never mutated.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `phase` | `Phase` | `UNINITIALIZED` | lifecycle state |
| `energy_wh` | `float` | `0.0` | the accumulator `E`, in Wh |
| `demand` | `Demand` | `NO_DEMAND` | last demand **actually emitted** (post-mask) |
| `allowed` | `frozenset[Demand]` | `ALL_DEMANDS` | supervisor mask; always contains `NO_DEMAND` |
| `t_last` | `float \| None` | `None` | integration reference, monotonic s |
| `t_healthy` | `float \| None` | `None` | last `evaluate` at which `resolve` succeeded, monotonic s |
| `signals` | `SignalTable` | empty | the zero-order-hold table |
| `integrated_s` | `float` | `0.0` | lifetime seconds that were integrated |
| `skipped_s` | `float` | `0.0` | lifetime seconds advanced over without integrating |
| `ledger` | `EnergyLedger` | empty | lifetime Wh per balance term |
| `clamped` | `bool` | `False` | the last *integrated* step hit the anti-windup bound |

`integrated_s + skipped_s` equals elapsed time since the clock was seeded ([A-22]).

The `HOLD → FAULT` timer measures from `t_healthy`, which advances on every `evaluate` that
resolves — not on every reading. A required signal can be 599 s old and the loop is still `RUNNING`.

### 2.6 Pure functions

None of these mutate their arguments, read a clock, or raise on any float value including `NaN` and
`±inf`. Each returns fresh values.

#### Physics and relay

```python
energy_balance(inputs: Inputs, cfg: Config) -> Balance
```
Computes the instantaneous balance. Non-finite fields in `inputs` propagate to the output
(`NaN` in → `NaN` out); the core never constructs such an `Inputs` itself, because `observe`
filters non-finite readings first. The trim **deviation** is clamped at `±cfg.trim_limit_k`, not
the resulting power ([A-14]).

```python
relay(energy_wh: float, previous: Demand, hysteresis_wh: float) -> Demand
```
One transition at most. `previous` must be the demand *actually emitted* last, post-mask.
Thresholds are inclusive: `E <= -H` engages heating, `E >= +H` engages cooling, `E >= 0` releases
heating, `E <= 0` releases cooling. There is no `HEATING → COOLING` edge. `NaN` energy holds the
previous demand (every comparison is false).

```python
clamp_bounds(allowed: AbstractSet[Demand], cfg: Config) -> tuple[float, float]
```
Anti-windup bounds `(lo, hi)` on `E`. `±energy_clamp_factor * H` normally; a masked direction
tightens its side to exactly `∓H` so unmasking cannot release banked windup ([A-8]).
`clamp_bounds(frozenset(), cfg)` returns `(-H, +H)`.

```python
clamp(value: float, lo: float, hi: float) -> float
```
Scalar clamp. `NaN` passes through unchanged. Does not validate `lo <= hi`.

#### Signal resolution

```python
resolve(signals: SignalTable, at: float, cfg: Config) -> Inputs | None
```
Resolves all five signals to the instant `at`. Returns `None` if **any** required signal is missing
or older than its `max_age_s` — the caller must then neither integrate nor decide. Stale optional
signals fall back to `cfg.fallback(signal)` and are listed in `Inputs.substituted`.

A non-finite `at` counts every signal as **stale**, so `resolve` returns `None` and agrees with
`SignalTable.unhealthy`. The staleness test is written `not (age <= max_age)` rather than
`age > max_age` precisely because the two differ under `NaN`.

#### Lifecycle

```python
initial_state() -> ControllerState
```
A cold `ControllerState` with all defaults.

```python
restore(snapshot: StateSnapshot | None, now_wall: float, cfg: Config) -> ControllerState
```
Warm start, or cold start if the snapshot is unusable. Falls back to `initial_state()` when the
snapshot is `None`, fails `is_valid()`, has a foreign `version`, or its age
(`now_wall - saved_at`) is non-finite, negative, or greater than `cfg.state_max_age_s`. An age of
*exactly* `state_max_age_s` is accepted. A restored `energy_wh` is clamped to
`clamp_bounds(ALL_DEMANDS, cfg)`.

Always returns `phase = UNINITIALIZED` and `t_last = None`: energy and relay phase are restored,
the clock is re-seeded by the first observation, and held sensor values are deliberately not
resurrected ([A-21]).

```python
to_snapshot(state: ControllerState, now_wall: float) -> StateSnapshot
```
Captures `energy_wh` and `demand`. `now_wall` is **wall** clock seconds; it is stored verbatim and
not validated.

#### Time, ingest, decide

```python
advance_to(state: ControllerState, t: float, cfg: Config) -> tuple[ControllerState, float, bool]
```
Moves the time reference to `t`, integrating the interval that just closed. Returns
`(state, dt_s, integrated)`. The **only** function that changes `energy_wh`, `ledger`,
`integrated_s`, `skipped_s` and `clamped`. Never changes `demand` or `phase`.

| Condition on `t` | Result |
|---|---|
| non-finite | `(state, 0.0, False)` — unchanged, reference does not move |
| `state.t_last is None` | `t_last := t`, `(state', 0.0, False)` — seed only, no integral |
| `dt = t - t_last <= 0` | `(state, 0.0, False)` — no time travel ([A-18]); `skipped_s` unchanged |
| `dt > cfg.max_step_s` | reference advances, `skipped_s += dt`, `(state', dt, False)` |
| `resolve(signals, t_last)` is `None` | reference advances, `skipped_s += dt`, `(state', dt, False)` |
| otherwise | integrates, clamps, `(state', dt, True)` |

Interval validity is judged **at the interval start** (`t_last`), because that is what zero-order
hold says was true throughout it. Output health is judged at the interval end.

```python
observe(state: ControllerState, reading: Reading, cfg: Config) -> ControllerState
```
Ingests one asynchronous reading, **integrate-then-update**: it calls `advance_to(reading.t)` to
close the open interval with the *old* held values, then applies the new value. Reversing those two
steps is the classic asynchronous-integration bug.

Silently rejected (state returned unchanged): a non-finite `value`, a non-finite `t`, or a `t`
strictly older than the timestamp already held **for that same signal**. A reading with a `t` equal
to the held timestamp is accepted and overwrites. A reading older than `t_last` but newer than its
own signal's timestamp is applied without rewinding the integral ([A-18]).

```python
observe_all(state: ControllerState, readings: Iterable[Reading], cfg: Config) -> ControllerState
```
Applies `observe` in iteration order. Pass readings in **delivery** order; the per-reading rules
handle ordering across signals. The iterable is consumed once.

```python
set_allowed(state: ControllerState, allowed: AbstractSet[Demand], t: float, cfg: Config) -> ControllerState
```
Changes the permitted directions. `NO_DEMAND` is always added. Tightening the mask re-clamps `E`
immediately so a masked direction cannot bank windup.

*Sharp edge:* if the resulting set equals the current `allowed`, the function returns the state
unchanged — it does **not** advance the clock. Do not rely on it as a heartbeat.

```python
evaluate(state: ControllerState, t: float, cfg: Config) -> tuple[ControllerState, Decision]
```
Advances to `t` and decides. The **only** function that changes `demand`. Must be called on a fixed
heartbeat even when nothing has been observed, or a quiet system never advances time and never
engages. Decision order:

1. `phase is UNINITIALIZED` and inputs unresolvable → `NO_DEMAND` / `WAITING_FOR_INPUTS`.
2. `phase is UNINITIALIZED` and inputs resolve → `phase := RUNNING`, emit `state.demand`
   (`NO_DEMAND` cold, the restored demand warm) / `SEEDED`.
3. `phase is FAULT` and inputs resolve → `E := 0`, `demand := NO_DEMAND`, `phase := RUNNING` /
   `FAULT_RECOVERED`.
4. inputs unresolvable → `HOLD` and coast if `t - t_healthy <= input_hold_s`, else `FAULT` and
   `NO_DEMAND`.
5. otherwise → `relay(E, state.demand, H)`, then the mask; store the **masked** result so the
   relay's `previous` is always what was emitted.

A non-finite `t` is rejected before step 1: the call is ignored entirely, **no state changes**, and
the returned `Decision` carries `reason=INVALID_TIME` with the current demand and phase. See §7 for
backwards clocks.

### 2.7 `Controller` — the mutable facade

```python
class Controller:
    def __init__(self, cfg: Config, state: ControllerState | None = None) -> None
```
Holds one state cell and nothing else: no clock, no I/O, no logging. Time is still supplied by the
caller on every call. `state=None` means `initial_state()`; pass the result of `restore()` for a
warm start.

| Member | Signature | Notes |
|---|---|---|
| `config` | `property -> Config` | the config given at construction; immutable |
| `state` | `property -> ControllerState` | current state, safe to keep (frozen) |
| `energy_wh` | `property -> float` | shortcut for `state.energy_wh` |
| `demand` | `property -> Demand` | shortcut for `state.demand` |
| `phase` | `property -> Phase` | shortcut for `state.phase` |
| `ledger` | `property -> EnergyLedger` | shortcut for `state.ledger` |
| `advance(t: float) -> None` | | integrate up to `t` without deciding |
| `observe(signal: Signal, value: float, t: float) -> None` | | convenience over `observe_reading` |
| `observe_reading(reading: Reading) -> None` | | |
| `observe_all(readings: Iterable[Reading]) -> None` | | |
| `set_allowed(allowed: AbstractSet[Demand], t: float) -> None` | | |
| `evaluate(t: float) -> Decision` | | the only call that changes `demand` |
| `snapshot(now_wall: float) -> StateSnapshot` | | `now_wall` is wall clock, everything else monotonic |

There is no way to replace `cfg` on a live `Controller`. Reloading config means constructing a new
`Controller(new_cfg, old.state)` ([A-16]).

Cost, measured on CPython 3.14: ~6 µs per `observe`, ~7.5 µs per `evaluate`. Each `observe`
allocates a new `SignalTable` and backing dict.

---

## 3. `Config`

Frozen dataclass. Immutable within a call; reloading means constructing a new value.
`validate() -> list[str]` returns human-readable problems and **never raises**; an empty list means
valid. It is not called automatically — call it yourself at startup.

### 3.1 Tuning fields

Defaults are the legacy system's live field-tuned values, not guesses.

| Field | Type | Unit | Default | Valid range | Controls | `validate()` message |
|---|---|---|---|---|---|---|
| `desired_indoor_c` | `float` | °C | `22.0` | `5 … 30` inclusive, finite | target indoor temperature `T_set` | `desired_indoor_c {v} outside 5..30 degC` |
| `transmission_w_per_k` | `float` | W/K | `155.0` | `> 0`, finite | conduction + ventilation UA, **excluding** solar | `transmission_w_per_k must be > 0` |
| `adjustment_w_per_k` | `float` | W/K | `805.0` | `>= 0`, finite | trim stiffness: watts of model error tolerated per kelvin of standing offset. `0` disables the trim | `adjustment_w_per_k must be >= 0` |
| `hysteresis_wh` | `float` | Wh | `1500.0` | `> 0`, finite | `H`: engage at `∓H`, release at `0` | `hysteresis_wh must be > 0` |
| `trim_limit_k` | `float \| None` | K | `3.0` | `> 0` and finite, or `None` to disable | clamp on the **deviation** feeding the trim; bounds a stuck sensor | `trim_limit_k must be finite and > 0, or None to disable` |
| `solar_gain_factor` | `float` | — | `0.02` | `>= 0`, finite | adapter multiplies PV **DC** power by this to estimate solar gain — DC rather than inverter AC, so the battery's charge and discharge cannot corrupt it. **The core never reads it** | `solar_gain_factor must be >= 0` |

`ENERGY_UNIT: ClassVar[str] = "Wh"` is a class attribute, not a constructor field ([A-13]).

### 3.2 Time and safety fields

| Field | Type | Unit | Default | Valid range | Controls | `validate()` message |
|---|---|---|---|---|---|---|
| `energy_clamp_factor` | `float` | ×H | `2.0` | `>= 1`, finite | anti-windup bound `±factor * hysteresis_wh` | `energy_clamp_factor must be >= 1 (clamp must not bite inside the band)` |
| `max_step_s` | `float` | s | `300.0` | `> 0`, finite | longest interval still trusted for integration; longer gaps are skipped | `max_step_s must be > 0` |
| `tick_interval_s` | `float` | s | `60.0` | `> 0`, `<= max_step_s / 2`, finite | the decision heartbeat you are expected to honour | `tick_interval_s must be > 0` / `tick_interval_s {v} must be <= max_step_s/2 ({x}) or normal intervals will be skipped` |
| `integration_interval_s` | `float` | s | `10.0` | `> 0`, `<= tick_interval_s`, finite | the fast `advance()` tick | `integration_interval_s must be > 0` / `integration_interval_s must be <= tick_interval_s (it exists to integrate MORE often than the loop decides, not less)` |
| `input_hold_s` | `float` | s | `900.0` | `>= 0`, finite | how long to coast in `HOLD` before `FAULT` | `input_hold_s must be >= 0` |
| `state_max_age_s` | `float` | s | `900.0` | `>= 0`, finite | snapshot freshness window, on **wall** time | `state_max_age_s must be >= 0` |
| `persist_interval_s` | `float` | s | `60.0` | `> 0`, finite | snapshot write cadence. Advisory: the core never writes | `persist_interval_s must be > 0` |

Twelve fields are additionally checked for finiteness and report `{name} must be finite, got {v!r}`
when set to `NaN` or `±inf`: the five tuning fields other than `trim_limit_k` (which has its own
combined check), plus all seven above.

Derived, read-only:

```python
Config.energy_limit_wh -> float          # energy_clamp_factor * hysteresis_wh; 3000.0 by default
Config.max_age(signal: Signal) -> float  # max_age_s.get(signal, DEFAULT_MAX_AGE_S[signal])
Config.fallback(signal: Signal) -> float # fallback_w.get(signal, DEFAULT_FALLBACK_W.get(signal, 0.0))
```

`fallback()` returns `0.0` for a signal in neither map, so it is total over all five signals.

### 3.3 Per-signal maps

```python
max_age_s: Mapping[Signal, float] = DEFAULT_MAX_AGE_S
fallback_w: Mapping[Signal, float] = DEFAULT_FALLBACK_W
```

Partial maps are fine — missing keys fall through to the module defaults.

| Signal | Required | `max_age_s` default | `fallback_w` default | On expiry |
|---|---|---|---|---|
| `T_INDOOR` | yes | `600.0` | — | interval skipped; `HOLD` → `FAULT` |
| `T_OUTDOOR` | yes | `600.0` | — | interval skipped; `HOLD` → `FAULT` |
| `P_PRODUCED` | yes | `600.0` | — | interval skipped; `HOLD` → `FAULT` |
| `P_SOLAR` | no | `600.0` | `0.0` W | substituted, listed in `Decision.substituted` |
| `P_INTERNAL` | no | `math.inf` | `550.0` W | never expires by default; a live source should set a finite age |

`validate()` walks all five signals and reports:

- `max_age_s[{NAME}] must be > 0, got {v!r}` — for `NaN` or `<= 0`.
- `max_age_s[{NAME}] must be finite: a required signal that never expires cannot be detected as
  dead` — infinite age on a required signal.
- `fallback_w[{NAME}] must be finite, got {v!r}` — for every entry in `fallback_w`.

Tightening `max_age_s` is the most common way to make this loop worse.

---

## 4. Lifecycle contract

### 4.1 The call sequence

```
1.  construct     cfg = Config(); assert cfg.validate() == []
                  state = restore(store.load(), clock.now_wall(), cfg)
                  ctrl  = Controller(cfg, state)

2.  observe       for each reading as it arrives, at any rate:
                      ctrl.observe(signal, value, sample_t)

3.  advance       every integration_interval_s (10 s):
                      ctrl.advance(clock.now_monotonic())

4.  evaluate      every tick_interval_s (60 s):
                      decision = ctrl.evaluate(clock.now_monotonic())
                      sink.apply(decision)
                      diag.record(decision, ctrl.state)

5.  persist       on demand change, and at least every persist_interval_s:
                      store.save(ctrl.snapshot(clock.now_wall()))
```

Steps 2, 3 and 4 interleave freely. Step 3 is optional for correctness — the integral is exact at
either resolution over piecewise-constant signals — but it resolves a step in pump power six times
sooner and costs nothing ([A-24]).

### 4.2 Ordering rules

- **Integrate-then-update.** A reading describes the world from its timestamp onward and never
  before it. `observe` closes the open interval with the *old* held values first. This is done for
  you inside `observe`; do not pre-apply values to the signal table yourself.
- **Timestamps are sample time, not arrival time.** Never fold transport latency into
  `Reading.t`. A latency-inflated timestamp is indistinguishable from a broken controller.
- **Monotonic clock for readings and for every `t` argument** on `advance`, `observe`,
  `set_allowed` and `evaluate`. It must not jump on NTP correction, DST, or suspend/resume.
- **Wall clock only for `snapshot(now_wall)` and `restore(..., now_wall, ...)`.** Nothing else.
  Mixing the two silently breaks snapshot freshness ([A-12]).
- **`evaluate` on a fixed heartbeat regardless of activity.** Nothing else advances `demand`.
- **`advance` never changes `demand` or `phase`.** Only `evaluate` does.
- Within one tick, observe everything that has arrived *before* calling `evaluate`, so the decision
  sees the freshest world.

### 4.3 What the core guarantees

- **Purity.** No argument is mutated. Every returned value is fresh and frozen. The same
  `(state, event, cfg)` always produces the same `(state', output)`.
- **Totality.** No core function raises on any `float` value, including `NaN` and `±inf`. There are
  no `assert`s and no exceptions in the control path. (`Config.max_age` would raise `KeyError` on a
  non-`Signal` key, which the type system prevents.)
- **No I/O, no clock, no randomness, no logging, no blocking.** Enforced mechanically by
  `tests/test_core.py::TestBoundary::test_core_imports_no_io_no_clock_no_randomness`, which walks
  the core's import AST and rejects anything outside
  `{__future__, dataclasses, enum, math, types, typing}`.
- **Always queryable.** `evaluate` returns a well-defined `Decision` at every instant, including
  before any input exists.
- **Bounded state.** `energy_wh` is clamped on every integration step and on every mask change.
  `SignalTable` holds at most five entries.

### 4.4 What the caller must supply

- A monotonic time source, and a wall-clock source used only for snapshots.
- The heartbeat. The core does not schedule itself.
- Plausibility filtering. The core rejects only non-finite floats. Range checks
  (`-60 °C ≤ T ≤ 60 °C`), unit conversion, and spatial averaging of indoor temperature belong to
  the adapter — one rule, one place. See §5.
- Correct sample timestamps on a monotonic scale.
- Single-writer discipline over a `Controller`. See §8.
- Atomic snapshot writes.

---

## 5. Ports

The interfaces an adapter satisfies. They are **specified but not yet
implemented in code** — there is no `ports` module, no `adapters/` package and no `runtime/loop`
in this repository. The shapes are stable enough to build against; the import path is not.

```
interface Clock:            now_monotonic() -> Mono ; now_wall() -> Wall
interface SensorSource:     poll(now: Mono) -> Iterable[Reading]     # may be empty
interface DemandSink:       apply(decision: Decision) -> None        # idempotent, called every tick
interface StateStore:       load() -> StateSnapshot | None ; save(snap) -> None
interface DiagnosticsSink:  record(decision, state) -> None
```

| Port | Responsibility | Notes for the implementer |
|---|---|---|
| `Clock` | the two time bases | `now_monotonic` must be immune to NTP and DST steps |
| `SensorSource` | produce `Reading`s | owns plausibility bounds, unit conversion, spatial averaging, and the `solar_gain_factor` multiplication. Must stamp *sample* time. Returning an empty iterable is normal |
| `DemandSink` | translate `Demand` into hardware | called on **every** tick, so it must be idempotent; it is also where minimum on/off dwell and any staging logic live ([A-15]) |
| `StateStore` | persist `StateSnapshot` | write atomically; a torn write must read back as `None` |
| `DiagnosticsSink` | logging and metrics | never in the control path; must not raise back into the loop |

The dependency rule: arrows point inward. `heatnrgx.core` must never import ports, runtime or
adapters. Enforce it with an import-lint test.

`heatnrgx.sim.runner.ControlStrategy` is a *different* protocol — it is what the simulator needs
from a controller (`observe_all` / `set_allowed` / `advance` / `evaluate`), not an adapter port.

---

## 6. Diagnostics

### 6.1 `Decision` fields

Every field, and what you use it for.

| Field | Type | Default | What it is | Use it for |
|---|---|---|---|---|
| `t` | `float` | — | the instant this decision describes (monotonic s) | correlating with the reading stream |
| `demand` | `Demand` | — | the output, post-mask | driving the plant |
| `reason` | `Reason` | — | why | log level, alerting |
| `phase` | `Phase` | — | lifecycle state after this call | health dashboards |
| `energy_wh` | `float` | — | `E` after this call, post-clamp | the single number that explains the demand |
| `hysteresis_wh` | `float` | — | `cfg.hysteresis_wh`, carried so the log line is self-contained | scaling `energy_wh` |
| `dt_s` | `float` | `0.0` | interval closed by **this call alone** | spotting a stalled heartbeat. Often `0.0` when a reading already closed the interval — that is healthy |
| `integrated_s` | `float` | `0.0` | **lifetime** seconds integrated | with `skipped_s`, the coverage ratio |
| `skipped_s` | `float` | `0.0` | **lifetime** seconds advanced over without integrating | the health signal for the whole time model. Rising in normal operation means the heartbeat is too slow or a source is dying ([A-22]) |
| `balance` | `Balance \| None` | `None` | the **instantaneous** balance at `t` | attributing the demand to a term. `None` whenever inputs did not resolve |
| `inputs` | `Inputs \| None` | `None` | the five resolved values | seeing exactly what the core believed |
| `ledger` | `EnergyLedger \| None` | `None` | lifetime Wh per term | model identification; clamp detection |
| `clamped` | `bool` | `False` | the last integrated step hit the anti-windup bound | detecting a plant that cannot follow |
| `unhealthy` | `frozenset[Signal]` | `frozenset()` | required signals missing or stale **at `t`** | per-sensor alerting |
| `substituted` | `frozenset[Signal]` | `frozenset()` | optional signals running on their fallback | quantifying model bias |

Derived, read-only:

```python
Decision.engage_heating_at_wh -> float   # -hysteresis_wh
Decision.engage_cooling_at_wh -> float   # +hysteresis_wh
Decision.release_at_wh        -> float   # 0.0
```

`integrated_s` and `skipped_s` are lifetime totals, not per-call deltas, on purpose: readings close
intervals *between* ticks, so a per-call boolean would report a perfectly healthy loop as skipping
every interval ([A-22]). To get a per-window figure, difference two decisions.

Because `ledger.total_wh` keeps running past the clamp, `ledger.total_wh - energy_wh` is exactly
the energy the anti-windup bound has discarded. Measured on a 5-hour pinned run:
`E = -3000.0 Wh`, `ledger.total_wh = -14925.0 Wh`, gap `-11925.0 Wh`.

### 6.2 Reading `describe()`

`Decision.describe() -> str` renders a one-line log form: a fixed prefix, then four suffixes that
appear only when they are non-trivial. This is a real line, from a controller pinned at its
anti-windup bound whose optional gains were never published and whose heartbeat ran 400 s late:

```
t=3820.0 HEATING (normal/running) E=-3000.0Wh of +-1500 net=-3185W dt=400.0s skipped=400s CLAMPED sub=['P_INTERNAL', 'P_SOLAR']
│        │        │      │        │              │      │          │         │            │       │
│        │        │      │        │              │      │          │         │            │       └─ substituted    optional signals on their fallback (omitted when empty)
│        │        │      │        │              │      │          │         │            └─ clamped        E pinned at the anti-windup bound (omitted when False)
│        │        │      │        │              │      │          │         └─ skipped_s      LIFETIME seconds never integrated (omitted when 0)
│        │        │      │        │              │      │          └─ dt_s           interval closed by THIS call alone
│        │        │      │        │              │      └─ balance.p_net_w  instantaneous balance; renders p_net=n/a if inputs did not resolve
│        │        │      │        │              └─ hysteresis_wh  the +- engage threshold
│        │        │      │        └─ energy_wh      the accumulator E, always signed
│        │        │      └─ phase.value    lifecycle state after this call
│        │        └─ reason.value   why -- diagnostics only, never control flow
│        └─ demand         the output, post-mask
└─ Decision.t     decision instant, monotonic seconds
```

The fifth suffix, `stale=[...]` from `unhealthy`, can never share a line with a resolved `net=`: a
required signal that is stale at `t` is exactly what makes `resolve` return `None`. Seeing `stale=`
means the balance is `n/a` and the phase is `UNINITIALIZED`, `HOLD` or `FAULT`.

Real lines, in order of severity:

```
t=0.0    NO_DEMAND (waiting_for_inputs/uninitialized) E=+0.0Wh of +-1500 net=p_net=n/a dt=0.0s stale=['P_PRODUCED', 'T_INDOOR', 'T_OUTDOOR']
t=0.0    NO_DEMAND (seeded/running)      E=+0.0Wh    of +-1500 net=-2985W    dt=0.0s
t=60.0   NO_DEMAND (normal/running)      E=-49.8Wh   of +-1500 net=-2985W    dt=60.0s
t=60.0   NO_DEMAND (normal/running)      E=-53.1Wh   of +-1500 net=-3185W    dt=60.0s sub=['P_INTERNAL', 'P_SOLAR']
t=1860.0 HEATING   (normal/running)      E=-1542.2Wh of +-1500 net=-2985W    dt=0.0s
t=2740.0 HEATING   (normal/running)      E=-1940.2Wh of +-1500 net=-2985W    dt=400.0s skipped=400s
t=3660.0 HEATING   (normal/running)      E=-3000.0Wh of +-1500 net=-2985W    dt=0.0s CLAMPED
t=700.0  NO_DEMAND (held_stale_input/hold)  E=+0.0Wh of +-1500 net=p_net=n/a dt=700.0s skipped=700s stale=['P_PRODUCED', 'T_INDOOR', 'T_OUTDOOR']
t=2000.0 NO_DEMAND (fault_timeout/fault)    E=+0.0Wh of +-1500 net=p_net=n/a dt=1300.0s skipped=2000s stale=['P_PRODUCED', 'T_INDOOR', 'T_OUTDOOR']
t=2760.0 NO_DEMAND (fault_recovered/running) E=+0.0Wh of +-1500 net=-2985W   dt=0.0s skipped=2100s
```

(Gathered from several runs; columns aligned here for reading, `describe()` emits single spaces.
The `net=p_net=n/a` doubling is what the code produces — a cosmetic quirk of the formatter, not
two fields.)

What to watch:

| Symptom | Means |
|---|---|
| `dt=0.0s` on a normal tick | a reading already closed the interval. Healthy |
| `dt` ≫ `tick_interval_s` | the heartbeat is late; if `dt > max_step_s` the interval is lost |
| `skipped_s` rising while `RUNNING` | heartbeat too slow, or a source dying ([A-19]) |
| `CLAMPED` for long stretches | the plant cannot follow the demand: undersized, defrosting, locked out, or masked |
| `sub=` permanently non-empty | a gain source is not wired up; you are paying a standing offset of `p_model_error / a_trim` kelvin |
| `stale=` at all | a required source has missed its `max_age_s`; the phase is `uninitialized`, `hold` or `fault`, never `running`. The listed names are the sources to chase |
| `stale=` with `phase=running` | an optional signal is stale (harmless, it is on its fallback), or the required set went stale in the same tick that recovered |

---

## 7. Failure behaviour

| Situation | What the core does | What the `Decision` looks like | What the adapter should do |
|---|---|---|---|
| **Missing signal at startup** — a required signal never observed | Stays `UNINITIALIZED`; `advance_to` seeds `t_last` but integrates nothing | `demand=NO_DEMAND`, `reason=WAITING_FOR_INPUTS`, `phase=UNINITIALIZED`, `balance=None`, `inputs=None`, `unhealthy` lists the missing ones | Hold the plant off. Alert if it persists past a few heartbeats — check `unhealthy` for which source |
| **Required signal goes stale** — no reading for `> max_age_s` | Intervals starting after expiry contribute zero energy; `E` frozen; demand coasted for up to `input_hold_s` measured from `t_healthy`, then forced off | `HOLD` / `HELD_STALE_INPUT` with the previous demand, then `FAULT` / `FAULT_TIMEOUT` with `NO_DEMAND`. `balance=None`, `skipped_s` climbing | Keep driving the last demand during `HOLD` — do not cycle the compressor on a routine dropout ([A-11]). On `FAULT`, shut off and page |
| **Recovery from `FAULT`** — required signals return | One evaluate resets `E := 0`, `demand := NO_DEMAND`, `phase := RUNNING`. Charge estimated across an unobserved interval is not trustworthy; zero is the neutral prior | one decision with `reason=FAULT_RECOVERED`, `energy_wh=0.0` | Nothing special. Expect up to one hysteresis of delay before the loop re-engages; that is by design ([A-9]) |
| **Optional signal goes stale** | Substituted with `cfg.fallback(signal)` — `0 W` solar, `550 W` internal. The loop keeps running | `phase=RUNNING`, `reason=NORMAL`, the signal listed in `substituted` (not in `unhealthy`) | Log at `INFO`. If it stays substituted, fix the source: the resulting model bias becomes a standing temperature offset ([A-25]) |
| **Non-finite reading** — `NaN`/`±inf` in `value` or `t` | `observe` returns the state unchanged. Nothing is integrated, the reference does not move, the held value survives | invisible: nothing in the `Decision` marks it. It looks exactly like a dropped reading | Count and log these at the adapter. The core deliberately will not tell you |
| **Out-of-order reading** — older than the value already held for that signal | Dropped silently. A reading older than `t_last` but *newer* than its own signal's timestamp **is** applied, without rewinding the integral ([A-18]) | invisible | Do not build a reorder buffer. Independent link latencies make this routine and the core is designed for it |
| **Clock goes backwards** — `t <= t_last` | `advance_to` returns `dt = 0`, integrates nothing, leaves `t_last` alone. `evaluate` still resolves and decides **at the earlier `t`**, and stamps `Decision.t` with it | `dt_s=0.0`, `skipped_s` unchanged (a rejected sample is not lost time), demand still updated | Use a real monotonic clock. If `Decision.t` ever goes backwards in your log, your clock is wrong, not the core |
| **Gap longer than `max_step_s`** | The interval contributes **zero** energy, `t_last` jumps forward, `skipped_s += dt`. Never invent energy nobody observed ([A-19]) | `skipped_s` jumps. `reason` is still `NORMAL` and `phase` still `RUNNING` if the signals are within `max_age_s` — a skipped interval is not by itself an unhealthy decision | Investigate the scheduler. A skipped interval under-states the deficit; the trim repays it from measured temperature |
| **Accumulator hits its clamp** | `E` pinned at `±energy_clamp_factor * hysteresis_wh` (`±3000 Wh` by default). `ledger` keeps accumulating past it | `clamped=True`, `CLAMPED` in `describe()`, `ledger.total_wh` diverging from `energy_wh` | Expected while the plant cannot follow. Sustained clamping means undersized capacity, a stuck diverter, or an external lockout |
| **Masked direction** — the relay wants a direction `set_allowed` forbids | Demand forced to `NO_DEMAND`; the clamp on that side tightens to exactly `∓H`, so unmasking gives an immediate but bounded response ([A-8]) | `demand=NO_DEMAND`, `reason=MASKED`, `energy_wh` pinned at `±H`, `clamped=True` | Nothing. This is the supervisor's own instruction coming back |

### A reading with a far-future timestamp

`observe` advances the time reference to `reading.t`, which instantly ages every *other* signal.
One `T_INDOOR` stamped an hour ahead on a controller seeded at `t=0` pushes the remaining required
signals past `max_age_s`, and the next `evaluate` reports `FAULT`. Adapters must bound timestamps,
not only values — a sensor with a wrong clock is as damaging as one with a wrong reading.

### Non-finite `t`

Every entry point rejects a non-finite instant without changing state. `observe` drops the reading;
`advance_to` and `advance` return unchanged; `evaluate` returns `reason=INVALID_TIME` carrying the
current demand and phase.

This was not always so. An earlier build guarded `t` in `advance_to` but not in `evaluate`, and
`resolve` read `NaN > max_age` as "fresh" — so a single `NaN` tick set `t_healthy := NaN`, which
silently voided the `HOLD` grace period and sent the *next* stale evaluate straight to `FAULT`
instead of coasting. Both holes are closed, and
`tests/test_core.py::TestInvalidTime` pins the behaviour.

A non-finite instant is always an adapter bug. `INVALID_TIME` is worth logging at error level: the
core will keep running, but it is telling you your clock source is broken.

### Timestamps far in the future

A single reading stamped far ahead of the current reference moves `t_last` to it, which instantly
ages every other signal past its `max_age_s`. Measured: one `T_INDOOR` reading at `t = 100000` on a
controller seeded at `t = 0` produced `phase=FAULT` on the next evaluate. Plausibility-bound
timestamps in the adapter, not just values.

---

## 8. Threading and reentrancy

**The pure functions are thread-safe and reentrant.** `energy_balance`, `relay`, `clamp`,
`clamp_bounds`, `resolve`, `initial_state`, `restore`, `to_snapshot`, `advance_to`, `observe`,
`observe_all`, `set_allowed` and `evaluate` hold no module-level mutable state, mutate no argument,
and return frozen values. Calling them concurrently on the same `ControllerState` from any number
of threads is safe; each caller simply gets its own successor state. There is no lock to contend
for and nothing to block on.

**The `Controller` facade is not thread-safe.** Every method is a non-atomic read-modify-write of
one cell:

```python
self._state = observe(self._state, reading, self._cfg)
```

Two threads interleaving there lose one update — silently, and in a way that corrupts the energy
integral rather than raising. `Controller` assumes a **single writer**. Either confine it to one
thread or event loop, or wrap every mutating call (`advance`, `observe`, `observe_reading`,
`observe_all`, `set_allowed`, `evaluate`) in one lock.

Reading is safe under that discipline: `state`, `energy_wh`, `demand`, `phase` and `ledger` return
frozen values or immutable scalars, so a reader thread can snapshot `ctrl.state` without a lock and
work on it. It may be one update stale; it will never be torn.

Other properties worth knowing:

| Property | Status |
|---|---|
| Reentrancy from a callback or signal handler | Nothing in the core calls out, so there is no reentrancy hazard — but `Controller`'s read-modify-write is not async-signal-safe |
| Blocking | Never. No I/O, no locks, no sleeps |
| `Config`, `SignalTable`, `ControllerState` hashability | **Unhashable** — each wraps a `dict` behind a `MappingProxyType`. Do not use them as dict keys or set members |
| `Reading`, `Inputs`, `Balance`, `EnergyLedger`, `Decision`, `StateSnapshot`, `Held` hashability | Hashable and safe to share across threads |
| Aliasing a mutable `max_age_s` / `fallback_w` | `Config` stores the mapping you pass by reference. Hand it a plain `dict` and later mutate it and you change controller behaviour mid-run with no revalidation. Pass `MappingProxyType` or a throwaway dict |
| Multiple `Controller`s | Fully independent. Sharing one `Config` between them is safe — it is immutable |
| `Decision` objects | Frozen; safe to hand to a logging thread |
