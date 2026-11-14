---
stage: doing
blocked: question
round: 0
attempts: 0
crashes: 0
pull_request: 31
comments_read: []
handed_over: null
claim: null
journal:
- number: 1
  speaker: human
  kind: created
  at: '2026-11-14T08:04:45.784040Z'
  stage: doing
- number: 2
  speaker: factory
  kind: started
  at: '2026-11-14T08:04:45.790500Z'
  stage: doing
- number: 3
  speaker: builder
  kind: question
  at: '2026-11-14T08:20:09.816570Z'
  stage: doing
  run: 4f907fa602ce473ab9ce9ec51ee52866
  blocked: question
---

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

1. 2026-11-14 human: created.
2. factory: started doing as heishamon.
3. builder: Stopped at criterion 10. Everything else is built, `make check` is green and the smoke test prints `1`. But the D1 mini's global RAM grows by 272 bytes against `develop` (50628 → 50900), and the cap is 64. The story's own required fields make this unreachable without a change outside its scope.

   **Where the 272 bytes go** (measured with nm on both ELF files, built from `git archive develop` and from the working tree):
   - `.bss` +136. The global `settings` grows by 16 (two doubles), `config` by 16, `loop_state` by 16 (the integral and the ledger's `integral_wh`) and `record` by 16 (the decision's `balance.p_integral_w` and `ledger.integral_wh`). The rest, +68, is `last_report` in `ctl_web.cpp`: `ctl_form_report` keeps a 32-byte name plus one result byte for each of `CTL_SETTINGS_NAMES_MAX` settings, so every new setting costs about 34 bytes there.
   - `.rodata` +136 (it sits in RAM on the ESP8266). This is the two setting names and their `FIXED_NAMES` entries, `status/p_integral_w`, `status/integral_wh`, `integral_reset` and the log line `control: integral reset`.
   - The two settings alone, through the settings struct, the loop config and the form report, already pass 64.

   **The options I see:**
   - **A.** Raise the cap in criterion 10 to about 300 bytes and accept the growth. Nothing else changes.
   - **B.** Keep 64 and pay it back by shrinking `ctl_form_report`. It would keep each setting's index in the name list instead of its 32-byte name, saving about 31 × 51 ≈ 1.5 KB, so the firmware ends well below `develop`. This changes `glue/ctl_form.c`, `ctl_form.h` and `ctl_web.cpp`, plus one check in `tests/control/test_form.c` (line 49 compares `report->name[index]`). That check belongs to another story, so I need your word to change it.
   - **C.** Keep 64 and claw bytes back elsewhere: write `status/`, `inputs/` and `outputs/` once in `ctl_telemetry.c` instead of in every subtopic (about 180 bytes) and drop the reset log line (24). It touches telemetry code beyond the story, and it is not certain to reach 64.

   I would choose **B**: this owner's board has already restarted from RAM pressure on the control page, and B pays back far more than this story costs. If you choose B, name the `test_form.c` check change. Whatever the answer, the rest stays as it is.

   **What is built (working tree):**
   - Core:
     - `core/ctl_config.{h,c}`: `integral_w_per_kh` (default 0, allowed 0 to 200) and `integral_limit_w` (default 300, above 0 up to 2000), with problem bits `CTL_CONFIG_BAD_INTEGRAL_GAIN` (1<<13) and `CTL_CONFIG_BAD_INTEGRAL_LIMIT` (1<<14).
     - `core/ctl_balance.{h,c}`: the balance gets `p_integral_w` and the ledger `integral_wh`; `ctl_ledger_total_wh` adds `integral_wh` last. New functions: `ctl_balance_add_integral`, `ctl_trim_deviation` (previously a static helper) and `ctl_trim_within_limit`.
     - `core/ctl_controller.{h,c}`: the state gets `p_integral_w`. The learning happens in `integrate()`, called from `ctl_advance`. New function: `ctl_reset_integral(state, t, config)`, which advances to `t` and then sets the integral to 0.
   - Glue:
     - `glue/ctl_settings.{h,c}`: the two settings, listed right after `trim_limit_k`; `CTL_SETTINGS_FIXED_NAMES` goes to 21. Their ranges are the core's, so a value out of range is refused as `INVALID_SET`.
     - `glue/ctl_dispatch.{h,c}`: a new topic kind `CTL_TOPIC_INTEGRAL_RESET` for `<base>/control/set/integral_reset`.
     - `glue/ctl_firmware.cpp`: that kind calls `ctl_reset_integral` and saves nothing.
     - `glue/ctl_telemetry.{h,c}`: two more status entries, `status/p_integral_w` and `status/integral_wh`, at indexes 10 and 11.
   - Tests:
     - New `tests/control/test_integral.c` covers criteria 2 to 6 and 8. For criterion 1 it also replays every golden trace on two controllers, one with the defaults and one with `integral_limit_w` 50, and requires them to match bit for bit. `test_traces.c` proves the default one against the traces.
     - `tests/control/test_settings.c` gains default rows, accept and refuse rows, `to_loop` checks and a check that a file without the two settings keeps their defaults. Its name-list indexes and counts shift by 2.
     - `tests/control/test_telemetry.c` gains the two status rows; the status count goes from 10 to 12, and the refused index from 10 to 12.
   - Docs: `docs/control/settings.md`, `docs/control/core.md`, and one new entry in `docs/control/decisions.md`.

   **Decisions I took:**
   - The integral is added to `p_net_w` only when it is not exactly 0. Adding `+0.0` unconditionally would turn a `-0.0` into `0.0`, and the traces compare signed zeros.
   - "The energy was not clamped" means the energy lay strictly between the bounds of the current mask at the interval's start. I rejected using the `clamped` flag alone, because `ctl_set_allowed` clamps the energy without setting it.
   - "Inside the trim limit" includes a deviation equal to the limit.
   - The integral is clamped to the limit in every interval `ctl_advance` closes, skipped ones included, so that a lowered limit takes effect at once.
   - Nothing publishes the other ledger terms today, so there was no ledger topic to follow. I chose `status/integral_wh`.
   - `status/p_integral_w` shows `-` without inputs, as `status/p_net_w` does, because it is read from the decision's balance; a separate decision field would cost RAM in every copy.

   **Noticed, not touched:**
   - The wire protocol (`wire/ctl_wire.c`) and `replay.py` know neither the new configuration keys nor the sixth ledger term. I left them alone so the protocol and the traces stay as they are.
   - The Python reference has no integral term.
   - `test_integral.c`'s twin replay starts from the default configuration rather than each trace's own `config` lines, because `trace.c`'s parser is static and not mine to change.
