# Golden traces

**Status:** format version 1. Exporter and reference replayer in `heatnrgx/sim/golden.py`,
committed set in `golden/`.

A golden trace is the exact stream of calls the simulator makes into the Python core, with the
core's state after each call. A port of the core (the C core of `firmware-design.md`) passes a trace
when, started cold and fed the same calls in the same order, it reproduces every recorded value.

```
python -m heatnrgx.sim.golden                     # regenerate the committed set in golden/
python -m heatnrgx.sim.golden --check golden      # replay it through the Python core
python -m heatnrgx.sim.golden flaky_transport --hours 6 --plain --out out/golden
```

---

## 1. What is recorded, and why exactly

The exporter is a stand-in for the controller the runner builds (`Recorder`, a `ControlStrategy`).
The runner is not changed: it hands the recorder each reading as the transport delivers it, each
10 s advance, each mask change and each 60 s decision, and the recorder passes each one to a real
`Controller` and writes down the call and the state after it. A test runs the same scenario with
and without the recorder and requires identical simulation rows.

The core uses `+`, `−`, `×`, `÷` and comparisons, and nothing from `math` beyond `isfinite`,
`isnan` and `inf`. Every value in a trace is therefore reproducible **bit for bit** by a port that
performs the same operations in the same order on IEEE-754 doubles. In C that means `double`
throughout, the same operation order as the Python source, and no floating-point contraction
(`-ffp-contract=off`): a fused multiply-add rounds once where the core rounds twice. A port that
cannot meet exact equality is wrong in a way worth finding, not a case for a tolerance.

Traces start cold: the firmware never restores a snapshot (`firmware-design.md` **[F-5]**), so
`restore` and `snapshot` are not exercised.

## 2. The committed set

| Trace | Source | What it is there for |
|---|---|---|
| 11 library scenarios | `heatnrgx.sim.scenario.catalog()`, cut to 9–24 h | the control law under synthetic weather, transport jitter, loss, out-of-order delivery, DHW diversion, a sensor outage, a mask |
| `barbarastr_march` | the house model under measured weather, 10–14 March 2023, indoor thermometer silent 60 min from 101.9 h | real PV, a real building, HOLD during a heating pulse; the chronogram's run |
| `edge_cases` | scripted, `golden.edge_cases()` | what the simulator never produces: NaN and infinite values and instants, a clock going backwards, a duplicate instant, a stale duplicate reading, a reading older than the integration reference **[A-18]**, a gap over `max_step_s`, mask changes that re-clamp E |

Each library scenario keeps its own seed; a cut changes nothing before it, because the simulator
is causal. `edge_cases` alone reaches every demand, reason and phase and both values of the clamp
flag; the test suite requires that of it and of the set.

The files are gzip, written without a timestamp so that regenerating an unchanged set reproduces
the same bytes. About 4 MB in all, 330 k calls.

**When the core changes**, `tests/test_golden.py` fails on the committed set. That is intended:
regenerate and commit the traces with the core change, so the port's reference moves in the same
commit. A change to a *scenario* does not fail it; the committed files are replayed, not compared
against a fresh export.

## 3. Format

ASCII text, one item per line, fields separated by single spaces. Blank lines are allowed. Lines
starting with `#` are comments, ignored. A reader must reject anything else it does not recognise.

```
heatnrgx-golden 1
# comments
scenario flaky_transport
config desired_indoor_c 22.0
config trim_limit_k 3.0
config max_age_s.P_INTERNAL inf
config fallback_w.P_INTERNAL 550.0
...
begin
observe 50.0 P_SOLAR 300.0 | E=-12.25 int=10.0 skip=40.0 clamped=0
advance 60.0 | E=-23.666666666666668 int=20.0 skip=40.0 clamped=0
allow 5400.0 COOLING,NO_DEMAND | E=-1500.0 int=5360.0 skip=40.0 clamped=1
evaluate 5400.0 | demand=NO_DEMAND reason=MASKED phase=RUNNING E=-1500.0 dt=0.0 int=5360.0 skip=40.0 clamped=1 pnet=-4410.0 unhealthy=- substituted=P_INTERNAL ledger=0.0,50.83333333333337,818.8888888888972,-7384.888888888805,0.0
...
end 3236
```

### 3.1 Header

| Line | Meaning |
|---|---|
| `heatnrgx-golden 1` | first line, exactly. A different version is a different format. |
| `scenario <name>` | informational |
| `config <key> <value>` | one per field of `heatnrgx.core.Config`. Per-signal maps are expanded as `max_age_s.<SIGNAL>` (all five signals) and `fallback_w.<SIGNAL>` (the two optional ones). `trim_limit_k` may be `none`. Fields the core does not read on the board (`solar_gain_factor`, `state_max_age_s`, `persist_interval_s`) are present and may be ignored. |
| `begin` | end of header |

### 3.2 Body

Each line is a call, a `|`, and the state after the call as `key=value` pairs in a fixed order.

| Call | Core function |
|---|---|
| `observe <t> <SIGNAL> <value>` | `observe(state, Reading(signal, value, t))` |
| `advance <t>` | `advance_to(state, t)` |
| `allow <t> <DEMANDS>` | `set_allowed(state, demands, t)`; `DEMANDS` as written by the caller, before the core adds `NO_DEMAND` |
| `evaluate <t>` | `evaluate(state, t)` |

After `observe`, `advance` and `allow`:

| Key | Value |
|---|---|
| `E` | `energy_wh` |
| `int`, `skip` | `integrated_s`, `skipped_s`, lifetime totals |
| `clamped` | `0` or `1`, the last integration hit the anti-windup bound |

After `evaluate`, the `Decision`: `demand`, `reason`, `phase`, `E`, `dt` (`dt_s`, the interval this
call closed), `int`, `skip`, `clamped`, `pnet` (`balance.p_net_w`, or `none` when no balance was
computed), `unhealthy` and `substituted` (signal sets), and `ledger` (the five lifetime terms in Wh:
produced, solar, internal, transmission, trim).

`end <n>` closes the body; `n` is the number of call lines, so a truncated file is detected.

### 3.3 Values

- **Numbers** are the shortest decimal text that converts back to the same IEEE-754 double:
  Python's `repr(float)`. C's `strtod` reads every one exactly, including `inf`, `-inf` and `nan`.
  Compare as doubles, never as text: the C side's `printf` will spell the same double differently.
  `nan` equals `nan` for this purpose; `-0.0` equals `0.0`.
- **Enums** are the Python member names: `HEATING`, `COOLING`, `NO_DEMAND`; `UNINITIALIZED`,
  `RUNNING`, `HOLD`, `FAULT`; `NORMAL`, `SEEDED`, `WAITING_FOR_INPUTS`, `MASKED`,
  `HELD_STALE_INPUT`, `FAULT_TIMEOUT`, `FAULT_RECOVERED`, `INVALID_TIME`; `T_INDOOR`, `T_OUTDOOR`,
  `P_PRODUCED`, `P_SOLAR`, `P_INTERNAL`.
- **Sets** are comma-separated names in alphabetical order, or `-` when empty.

## 4. Writing a port's replayer

`golden.replay` is the reference. A port's version, on the build PC:

1. Read the header into its config struct; reject an unknown key.
2. Start from the initial state.
3. For each call line, make the call, then compare every field after the `|`.
4. Report the first mismatch by line number and key, and keep going to a limit: the first
   mismatching line is the one to read, later ones are mostly its consequences.

Reading gzip in C needs zlib; `zcat golden/x.trace.gz | ./replay` avoids it.

## 5. What two of the traces show about the core

Both are current behaviour that a port must copy to pass, not defects introduced by the port:

- **Integration starts before seeding.** Once all required signals resolve, `advance_to` integrates,
  whatever the phase. E is already non-zero at the `SEEDED` decision (`edge_cases`: −23.7 Wh at
  60 s).
- **Fault recovery resets E but not the clamp flag.** The `FAULT_RECOVERED` decision reports
  `E=0.0` with `clamped=1` when the integral was pinned before the fault; the flag clears on the
  next integration. Diagnostics only.
