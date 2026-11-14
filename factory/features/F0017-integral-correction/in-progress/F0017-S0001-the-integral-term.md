---
stage: review
blocked: null
round: 1
attempts: 0
crashes: 0
pull_request: 31
comments_read: [6874460911, 6874676115]
handed_over: null
claim:
  instance: heishamon
  stage: review
  started_at: '2026-11-14T08:46:33.946640Z'
  lease_duration: 60
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
- number: 4
  speaker: human
  kind: comment
  at: '2026-11-14T08:22:01.144408Z'
  stage: doing
- number: 5
  speaker: factory
  kind: started
  at: '2026-11-14T08:22:03.730633Z'
  stage: doing
- number: 6
  speaker: builder
  kind: result
  at: '2026-11-14T08:32:33.002845Z'
  stage: doing
  run: 8e466ba76faf4a0d8a329f466df47d1b
- number: 7
  speaker: factory
  kind: started
  at: '2026-11-14T08:32:40.338827Z'
  stage: review
- number: 8
  speaker: reviewer
  kind: result
  at: '2026-11-14T08:35:04.767558Z'
  stage: review
  run: bae5f485c231438cb4b021e088808198
- number: 9
  speaker: factory
  kind: started
  at: '2026-11-14T08:35:13.751748Z'
  stage: doing
- number: 10
  speaker: builder
  kind: question
  at: '2026-11-14T08:39:37.753304Z'
  stage: doing
  run: 72b8a89ab4f9433e8f8c91b9950fb3a4
  blocked: question
- number: 11
  speaker: human
  kind: comment
  at: '2026-11-14T08:40:26.337385Z'
  stage: doing
- number: 12
  speaker: factory
  kind: started
  at: '2026-11-14T08:40:28.606181Z'
  stage: doing
- number: 13
  speaker: builder
  kind: result
  at: '2026-11-14T08:46:27.414995Z'
  stage: doing
  run: 7cc21784f00d4848879bf65b9c2dbf02
- number: 14
  speaker: factory
  kind: started
  at: '2026-11-14T08:46:33.946640Z'
  stage: review
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
4. 2026-11-14 human: B, thank you for the measurement.

   - Shrink `ctl_form_report` so that it keeps each setting's index in the name list instead of its name. Changes to `glue/ctl_form.c`, `glue/ctl_form.h` and `glue/ctl_web.cpp` are inside this story from now on.
   - My word for the test of another story: in `tests/control/test_form.c`, the check at line 49 that compares `report->name[index]` may change so that it checks the same thing through the index: the report's entry names the same setting (by `ctl_settings_name_at` of its index) and carries the same result. No other check of that file changes, and none may be removed or loosened.
   - Criterion 10 then reads: the D1 mini's global RAM is no larger than on `develop`. Report the before and after numbers in the result.
   - The page must render exactly as before after a save: same rows, same results, same order.

   Your other decisions stand as you took them, including `status/integral_wh` and the twin replay from the default configuration.
5. factory: started doing as heishamon.
6. builder: Built option B from the human's answer: the control page's save report now stores each setting's index instead of its name. The rest of the story is unchanged from entry 3. `make check` is green and the smoke test prints `1`.

   **Criterion 10, before and after (D1 mini, global RAM, from `make firmware`):** `develop` 50628 bytes (built from `git archive develop` in a scratch folder). The working tree is 49260 bytes, which is 1368 below `develop`. Both boards build. Criterion 11 also holds: `git diff develop --stat -- HeishaMon ':!HeishaMon/src'` is empty.

   **What I built this run:**
   - `HeishaMon/src/control/glue/ctl_form.h`: each `ctl_form_report` entry keeps `uint8_t setting[]` (its index in `ctl_settings_name_at`) instead of `char name[][32]`. The report also keeps a pointer to the settings it saved (`settings`) and their count of names after the save (`names`). The comment on `ctl_form_report_result` now says it returns no result once the count of names has changed.
   - `HeishaMon/src/control/glue/ctl_form.c`:
     - `report_setting` stores the index. A compile-time check makes sure `CTL_SETTINGS_NAMES_MAX` fits in a byte.
     - `ctl_form_report_result` looks names up through `ctl_settings_name_at`.
     - `ctl_form_row` looks the result up directly by the row's index, so a row is no longer found by comparing names.
     - `ctl_form_report_start` clears the new fields.
   - `HeishaMon/src/control/glue/ctl_web.cpp`: no change was needed. Its `last_report` is a zeroed static and it calls the same functions as before.
   - `tests/control/test_form.c`: only the check the human named (formerly line 49) changed. It now gets the name from `ctl_settings_name_at(&target.settings, report->setting[index], …)` and compares it with the expected name; the result checks beside it are as before. I moved the three static declarations (`form`, `report`, `target`) above `check_report_entry` so it can reach `target`. No check was removed or loosened.
   - New `tests/control/test_form_report.c`:
     - A save that raises `rooms` together with fields after it still reports each result under the right name, including on the rendered row.
     - When `rooms` changes after the save, the report shows no result.
     - A report that has been reset (`ctl_form_report_start`) holds no result.
   - `test_form_rows.c` and `test_lean_page.c` pass unchanged. They check that after a save the page shows the same rows, results and order as before.
   - `docs/control/decisions.md`: I added the report change, its reason and the rejected options A and C to this story's single entry, so the docs criterion still has one entry.

   **Decisions I took:**
   - **Indexes stay valid through a save.** Only `rooms` moves the names that follow it, and `ctl_form_apply` applies `rooms` before those names. So every index recorded during the save already matches the list as the save leaves it.
   - **When the count of names changes after a save, the report shows nothing.** This can happen if `rooms` is changed over MQTT between the save and the page that follows. Without this check, results would land on the wrong rows. I chose that blank results are safer than misplaced ones; it costs 5 bytes (pointer plus count).
   - **Rejected:** passing the settings into `ctl_form_report_result`. That would have meant changing calls in `test_form.c` and `test_form_rows.c`, which the human's permission does not cover.

   **Open:**
   - I tried to rewrite criterion 10 and the Interface in the work item's Description to match the human's answer in entry 4, as R5 says to. The tool's permission check refused the edit, so the Description still reads "no more than 64 bytes". The answer in entry 4 is the criterion I worked to, and it is met.

   **Noticed, not touched:** the same as entry 3. The wire protocol and `replay.py` do not know the new configuration keys or the sixth ledger term, and the Python reference has no integral term.

   `check`: check: green
7. factory: started review as heishamon.
8. reviewer: Findings:

   1. `HeishaMon/src/control/core/ctl_controller.h:2-6` and `docs/control/core.md` (lines 196-199, heading "The four functions" at 222, "The order of calls" at 296-309): these still say the controller has "four functions" and that "the other three call it [ctl_advance] first". `ctl_reset_integral` is now a fifth public function that advances the state and changes it. It is also missing from the order of calls. In addition, the header comment's third line was lengthened without reflowing it, so it now runs to about 135 characters. Fix: name the fifth function in the header comment and in core.md, add the reset to the order of calls, and reflow the comment.
   2. `docs/control/settings.md:42`: it says "`core.md` (*Energy balance*) says when it learns". The learning conditions are written under *Controller* ("The four functions", the paragraph that starts "`ctl_advance` is also where the integral term learns"). *Energy balance* only names the term. Fix: point to the *Controller* section.
   3. `docs/control/decisions.md`, the new entry's first sentence: "The balance gets a fourth power". `ctl_balance` already held four powers (transmission, model, trim, net). `p_integral_w` is the fifth, as core.md's table now shows. Fix: correct the count, or drop it.
   4. The work item's `## Description` does not match what was built or what entry 4 decided. Criterion 10 still reads "no more than 64 bytes", but entry 4 changed it to "no larger than on `develop`, before and after reported". The Interface does not name the `glue/ctl_form.c`, `glue/ctl_form.h` and `glue/ctl_web.cpp` changes, the new `tests/control/test_form_report.c`, or the human's leave for the one check in `tests/control/test_form.c`. Entry 4 also requires that the page renders exactly as before after a save, and that is not in the Description either. Fix: rewrite criterion 10 and the Interface, and add the rendering requirement as a criterion before the two closing ones (R5). The builder reported in entry 6 that its edit was refused. If the work item stays unwritable for the builder, it should end with `question` and name the path.

   Earlier tests:
   - `tests/control/test_form.c` (M): the human gave leave in entry 4 for the `report->name[index]` check. Only that check changed, and it now checks the same thing through `ctl_settings_name_at`. The three static declarations moved up so that check can reach `target`; no check was removed.
   - `tests/control/test_settings.c` (M): changed under the Interface's leave. It adds rows and checks for the two new settings. The name counts (19→21, 23→25, 21→23) and the name-list indexes (20/22/23 → 22/24/25) shift by the two new entries, and every earlier check still checks the same setting.
   - `tests/control/test_telemetry.c` (M): changed under the Interface's leave. It adds the two status rows. The status count (10→12) and the refused index (10→12) shift by the two new entries; no check was loosened.

   Checked and sound:
   - **Acceptance criteria 1–9 and 11:** each has a test that tests it.
     - Criterion 1: `test_traces.c` covers the default configuration, and the twin replay in `test_integral.c` covers `integral_limit_w` 50 with the gain at 0, as the human accepted in entry 4.
     - Criteria 6 and 7: both sides of every boundary are tested.
   - **Core:** with the gain at 0 the integral stays exactly 0.0, and `ctl_balance_add_integral` keeps a `-0.0` in `p_net_w`. The sum is `(p_model + p_trim) + p_integral`, in the reference's left-to-right order. Learning requires an integrated interval, and integration requires `ctl_signals_resolve`, which refuses when a required signal is not fresh.
   - **Save report:** `report->settings` points to the firmware's global settings (`ctl_firmware_settings()`), so it cannot dangle. A compile-time check makes sure the index fits in a byte. Every loop is bounded by `CTL_FORM_FIELDS_MAX`.
   - **Embedded safety:** no dynamic memory, no recursion, no unbounded loop. The firmware's reset handler calls `ctl_reset_integral` with `now_s()`, the same way the existing `ctl_set_allowed` call does.

   Not verified by me: the RAM numbers for criterion 10 (49260 against 50628 bytes on `develop`) are the builder's measurement. No stage check compares `make firmware` against `develop`.
9. factory: started doing as heishamon.
10. builder: Round 1. I fixed findings 1 to 3 from the review. Finding 4 is still open: I could not write the work item's Description, so I am asking.

    `make check` is green with the changes below, and the smoke test prints `1`.

    **Fixed:**
    1. `HeishaMon/src/control/core/ctl_controller.h`
       - The header comment now says "five functions" and "the other four call it first". It also says `ctl_advance` is where the integral term learns, and its lines are reflowed.
       - The comment on the state struct says "the five functions".

       `docs/control/core.md`
       - The Controller introduction says five functions, the other four, and that only `ctl_advance` and `ctl_reset_integral` change the integral term.
       - The heading is now "The five functions".
       - "The order of calls" gains step 4: `ctl_reset_integral` whenever the owner asks to clear the integral, with the instant of the request. The steps after it are renumbered (advance is 5, evaluate is 6).
    2. `docs/control/settings.md`: the pointer now names `core.md` (*Controller*, *The five functions*) instead of *Energy balance*. I reflowed the paragraph.
    3. `docs/control/decisions.md`: "a fourth power" is now "a fifth power".

    **Not done (finding 4):** I tried to rewrite the Description of `factory/features/F0017-integral-correction/in-progress/F0017-S0001-the-integral-term.md` to match entry 4. The tool's permission check refused the write again (it classed it as undermining oversight), as it did in entry 6. I did not try to get around that.

    **Question:** could you apply these three changes to that file's `## Description` yourself, or allow me to write it? The text I would use:
    - **Interface**, a new bullet before "No upstream file changes…": "The save report of the control page: `glue/ctl_form.h` and `glue/ctl_form.c` keep each setting's index in the settings list instead of its 32-byte name, and `glue/ctl_web.cpp` may change with them, so that the two new settings cost no RAM. A new host test `tests/control/test_form_report.c` covers the report. The leave for one check of another story: in `tests/control/test_form.c`, the check that compared `report->name[index]` checks the same thing through the index (the entry names the same setting by `ctl_settings_name_at` of its index and carries the same result). No other check of that file changes, and none is removed or loosened."
    - **Criterion 10:** "`make firmware` builds both boards; the D1 mini's global RAM is no larger than on `develop`, with the before and after numbers reported." Entry 6 measured `develop` at 50628 bytes and the branch at 49260.
    - **A new criterion 12**, before the two closing ones: "After a save, the control page renders exactly as before: the same rows, the same results, the same order." The current 12 and 13 become 13 and 14.

    **Options:**
    - **(a)** You edit the Description by hand. I would choose this, since the factory blocks my edit.
    - **(b)** You allow the write, and I make the edit on the next run.
    - **(c)** You accept entry 4 as the record of the changed criteria and leave the Description as it is.

    No code changed this round, and nothing else is open.
11. 2026-11-14 human: (c). My answer on this pull request (entry 4) is the record of the changed scope and criteria; the Description stays as it is. Reviewer: please judge criterion 10 and the page criterion by that answer, not by the Description's original text.

    Thank you for the RAM numbers: 50628 → 49260 bytes is exactly what I hoped for.
12. factory: started doing as heishamon.
13. builder: Round 1, after the human's answer in entry 11. Nothing was built or changed this run.

    - **Findings 1 to 3** were fixed and committed in entry 10. I checked that the fixes are in the committed tree:
      - `HeishaMon/src/control/core/ctl_controller.h` now says "five functions".
      - `docs/control/core.md` has the heading "The five functions" and the reset in the order of calls.
      - `docs/control/settings.md` points to *Controller*, *The five functions*.
      - `docs/control/decisions.md` now says "a fifth power".
    - **Finding 4:** the human chose (c). Entry 4 is the record of the changed scope and criteria, and the Description stays as it is. The reviewer judges criterion 10 and the page criterion by entry 4.

    **Results this run:**
    - `make check` ends with `check: green`.
    - Criterion 10: the D1 mini's global RAM is 49260 bytes, against 50628 on `develop` (entry 6's measurement of `develop`). The ESP32-S3 also builds.
    - The smoke test prints `1`, as expected.

    **Noticed, not touched:** the same as entry 6. The wire protocol and `replay.py` do not know the new configuration keys or the sixth ledger term, and the Python reference has no integral term.

    `check`: check: green
14. factory: started review as heishamon.
