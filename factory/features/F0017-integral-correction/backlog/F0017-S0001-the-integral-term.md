# The integral term

## Description

**Why.** The trim (`adjustment_w_per_k` times the clamped indoor error) is a proportional correction: a model error of some watts leaves the house steadily that many watts divided by the gain off its target. The owner's house sat about 0.4 K too warm on its first night under the fork. An integral term learns that missing power slowly and removes the offset.

**What it does.** The controller keeps one more number in its state, the integral power `p_integral_w`, starting at 0. While it may learn (below), each integrated interval adds `integral_w_per_kh × deviation × hours` to it, where `deviation` is the indoor error the trim uses (indoor minus target, clamped by the trim limit) and `hours` the interval's length in hours. The result is clamped to ±`integral_limit_w`. The balance adds the integral to the net power: `p_net_w = p_model_w + p_trim_w + p_integral_w`. The sign is the trim's: a house too warm makes the integral positive, a surplus, so the pump heats less.

**When it learns.** Only in an interval that the controller integrates into the energy, and only when, at that interval's start, every required signal was fresh, the indoor error was inside the trim limit (or the trim limit is off), and the energy was not clamped at its bound. In every other interval the integral holds its value. It never decays on its own.

**Off means off.** With `integral_w_per_kh` 0 the integral stays exactly 0.0, and `p_net_w` is computed with the same operations in the same order as today, so every golden trace replays bit for bit.

**Settings.** Two new settings, in the settings list right after `trim_limit_k`, stored in `/control.json` like the others and shown on the page like the others:

- `integral_w_per_kh`: W per kelvin per hour, 0 or more up to 200, default 0 (off).
- `integral_limit_w`: W, above 0 up to 2000, default 300.

Lowering `integral_limit_w` below the integral's size clamps the integral at once. Setting `integral_w_per_kh` to 0 freezes the integral at its value; setting `integral_reset` (below) clears it.

**A reset.** A command topic `<base>/control/set/integral_reset` with any payload sets the integral to 0. It is not a setting and is not stored.

**Telemetry.** `<base>/control/status/p_integral_w`, published with the other status values. The ledger gets a sixth term `integral_wh`, published like the other ledger terms.

## Interface

- The core: the state carries the integral, the configuration the two values with their rules (refused: NaN, infinite, outside the ranges above), the balance a field `p_integral_w`, the ledger a field `integral_wh`. `ctl_ledger_total_wh` adds `integral_wh` last. The learning happens where the energy is integrated, in `ctl_advance`.
- The glue: the two settings, the reset topic, the status and ledger telemetry.
- Tests: new host tests `tests/control/test_integral.c` (core) and rows in the existing settings tests for the two settings. The leave for tests another story wrote: `tests/control/test_config.c`, `tests/control/test_balance.c`, `tests/control/test_settings*.c`, `tests/control/test_telemetry*.c`, `tests/control/test_form*.c`, `tests/control/test_lean_page.c` and `tests/control/test_html.c` may gain rows or checks for the new fields and the settings list's new entries; no existing check may be removed or loosened.
- No upstream file changes, and no hook line changes.

## Acceptance criteria

1. Every golden trace replays bit for bit with the default configuration, and with `integral_limit_w` 50 and `integral_w_per_kh` 0.
2. A host test runs the controller with a constant indoor error of +0.5 K, fresh inputs and `integral_w_per_kh` 20 for 2 hours of 60 s intervals: the integral is 20.0 W (±1e-9), and `p_net_w` is larger than without the integral by exactly the integral.
3. The same with -0.5 K gives -20.0 W.
4. With `integral_w_per_kh` 100, `integral_limit_w` 30 and +1 K for 1 hour, the integral is 30.0 W; with -1 K, -30.0 W. Lowering `integral_limit_w` to 10 with the integral at 30 gives 10.0 W at the next interval.
5. The integral holds its value, rows of one test: an interval with a required signal not fresh; an interval that `ctl_advance` skips (longer than `max_step_s`); an indoor error of 5 K with `trim_limit_k` 3; an interval starting with the energy clamped.
6. `ctl_config_validate` refuses `integral_w_per_kh` -1, 201, NaN and infinity, and `integral_limit_w` 0, 2001, NaN and infinity; it accepts 0, 200, 1 and 2000.
7. The settings accept `integral_w_per_kh` 25 and `integral_limit_w` 300, refuse the values of criterion 6 with the old value kept, and list the two right after `trim_limit_k`; a `/control.json` without them loads with the defaults.
8. A message on `<base>/control/set/integral_reset` sets the integral to 0.0; nothing is written to `/control.json`.
9. The telemetry publishes `status/p_integral_w` and the ledger's `integral_wh`.
10. `make firmware` builds both boards; the D1 mini's global RAM grows by no more than 64 bytes against `develop`.
11. `git diff develop --stat -- HeishaMon ':!HeishaMon/src'` shows no upstream file changed.
12. `make check` stays green.
13. docs: `docs/control/settings.md` gets the two settings and the reset topic, with the start value 25 for `integral_w_per_kh` at `adjustment_w_per_k` 600 as a suggestion, not a default; `docs/control/core.md` describes the term, when it learns, and that it is off by default; `docs/control/decisions.md` gets one entry: an integral term against the steady offset, off by default, capped, learning only while it can be trusted; rejected: a higher proportional gain alone (it shrinks the offset but never removes it and pushes towards swinging).

## Smoke test

```sh
make test 2>&1 | grep -E '^control: [0-9]+ test programs passed$' | wc -l
```

Expect: `1`

## Journal
