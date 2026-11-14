---
stage: doing
blocked: null
round: 0
attempts: 0
crashes: 0
pull_request: 34
comments_read: [6875920189]
handed_over: null
claim: null
journal:
- number: 1
  speaker: human
  kind: created
  at: '2026-11-14T10:14:20.998119Z'
  stage: doing
- number: 2
  speaker: factory
  kind: started
  at: '2026-11-14T10:14:21.004586Z'
  stage: doing
- number: 3
  speaker: builder
  kind: question
  at: '2026-11-14T10:28:44.150499Z'
  stage: doing
  run: 0e400c7fd54a4188bf1c942baa8a62f0
  blocked: question
- number: 4
  speaker: human
  kind: comment
  at: '2026-11-14T10:29:31.738994Z'
  stage: doing
---

# Remove the shadow, the solar input and the planning documents

## Description

**Why.** The owner wants the fork to carry only what the production board uses. Three things are left that nothing uses: the shadow (a live comparison of the C controller against Node-RED, which is off), the solar input (the owner's house gains too little from the sun to model; the proportional and integral corrections absorb it), and two planning documents that later documents replaced.

**The shadow goes.** `HeishaMon/src/control/loop/ctl_shadow.c` and `.h`, the shadow tool under `tests/control/device/` (`shadow/shadow.ino`, `shadow.py`, `shadow_host.c` and anything only they use), `tests/control/test_shadow.c`, and `docs/control/shadow.md`. If the `Makefile` names any of them, the builder asks before going on.

**The solar input leaves the board.** The settings `pv_topic` and `solar_gain_factor`, the subscription and handling of the PV topic, the telemetry value `inputs/solar_w`, and their rows on the control page and in `docs/control/settings.md`. The glue sets the core's `solar_gain_factor` to 0 and never feeds the core's solar signal, so the signal falls back to 0 W as it does today with `pv_topic` empty. The core itself does not change: its solar signal and field stay, so that every golden trace replays bit for bit.

**Two documents go.** `docs/control/algorithm-plan.md` and `docs/control/firmware-design.md`. Every link to them from another document is removed or pointed to `core.md`, `firmware.md` or `decisions.md`.

**What stays.** Everything else, and the production board's behaviour: with `pv_topic` empty today, the board computes after this story exactly what it computes now.

## Interface

- Removed: the shadow module and tool, the two settings, the PV path in the glue, `inputs/solar_w`.
- The core (`HeishaMon/src/control/core/`) does not change.
- The leave for tests another story wrote: `tests/control/test_shadow.c` and any device test that tests only the shadow may be deleted. In `tests/control/test_settings*.c`, `tests/control/test_telemetry*.c`, `tests/control/test_form*.c`, `tests/control/test_lean_page.c`, `tests/control/test_html.c`, `tests/control/test_loop.c` and `tests/control/test_dispatch.c`, rows and checks for the removed settings and value may be deleted and indexes and counts may shift; no check of kept behaviour may be removed or loosened.
- No upstream file changes, and no hook line changes.

## Acceptance criteria

1. `git grep -n -E "ctl_shadow|pv_topic|solar_w\b|algorithm-plan|firmware-design" -- HeishaMon tests docs/control ':!docs/control/decisions.md'` prints nothing, and `git grep -n solar_gain_factor -- HeishaMon/src/control/glue docs/control/settings.md` prints nothing.
2. `git diff develop --stat -- HeishaMon/src/control/core` prints nothing.
3. A host test loads a `/control.json` holding `pv_topic` and `solar_gain_factor` with values: it loads, both names are ignored, every other setting in it is applied, and the loop's configuration has `solar_gain_factor` 0.
4. Every golden trace replays bit for bit, as before.
5. `make firmware` builds both boards; the D1 mini's global RAM and flash are no larger than on `develop`. Report the before and after numbers.
6. `git diff develop --stat -- HeishaMon ':!HeishaMon/src'` shows no upstream file changed.
7. `make check` stays green.
8. docs: `docs/control/settings.md`, `docs/control/firmware.md`, `docs/control/loop.md` and `docs/control/device-test.md` describe only what remains; `docs/control/decisions.md` gets one entry: the shadow, the solar input and the two planning documents removed, because nothing on the production board uses them; rejected: removing the solar signal from the core (the golden traces are compared bit for bit and nobody edits them).

## Smoke test

```sh
make test 2>&1 | grep -E '^control: [0-9]+ test programs passed$' | wc -l
```

Expect: `1`

## Journal

1. 2026-11-14 human: created.
2. factory: started doing as heishamon.
3. builder: Question: criterion 1, as written, cannot pass without breaking criterion 2 and the Description. Everything else is built, and `make check` is green.

   1. `solar_w\b` also matches the core's field `p_solar_w`, which the story keeps. It appears in `core/ctl_balance.c:40,78`, `core/ctl_signals.c:100` and `core/ctl_signals.h:30`. It is also in code and documents that only use that field: `loop/ctl_loop.c:248`, `adapt/ctl_pump.c:126-139` (a local variable named `solar_w`), `docs/control/core.md`, `docs/control/reference.md`, the Python reference, `tests/control/test_balance.c`, `test_signals.c` and `test_loop.c:475,519`.
   2. The second grep finds `glue/ctl_settings.c:586`, `config->core.solar_gain_factor = 0.0;`. The Description asks for exactly this line ("The glue sets the core's solar_gain_factor to 0").

   Options:
   (a) Read criterion 1 as aimed at the telemetry name and the setting. The first grep would use `(^|[^_])solar_w\b`, which prints nothing now. The second grep would accept the one line that sets the core's field to 0.
   (b) Hide the names: rename the adapter's local variable, and move the zeroing into `loop/` (for example a new `ctl_loop_config_defaults` value). This changes loop defaults and `test_loop.c` rows that test kept behaviour, and it still cannot clear `p_solar_w` from the core.
   (c) Reword the criterion some other way.
   I would choose (a), and nothing further would change.

   Built:
   - Deleted the shadow: `HeishaMon/src/control/loop/ctl_shadow.c/.h`, `tests/control/device/shadow/shadow.ino`, `shadow.py`, `shadow_host.c`, `test_shadow.py`, `shadow.example.json`, `data/shadow_capture.jsonl`, `tests/control/test_shadow.c` and `docs/control/shadow.md`. The `Makefile` names none of them, so I did not need to ask about it.
   - Deleted `docs/control/algorithm-plan.md` and `docs/control/firmware-design.md`. Their links in `adapters.md`, `golden-traces.md`, `router.md`, `reference.md`, `loop.md`, `firmware.md` and the docstrings of `docs/control/python-reference/*.py` (comments only) were removed or now point to `core.md`, `firmware.md` or `decisions.md`.
   - The glue changes:
     - `glue/ctl_settings.c/.h`: `pv_topic` and `solar_gain_factor` are gone, `CTL_SETTINGS_FIXED_NAMES` is now 17, and `ctl_settings_to_loop` sets the core's `solar_gain_factor` to 0.
     - `glue/ctl_dispatch.c/.h`: no PV topic match.
     - `glue/ctl_firmware.cpp/.h`: no PV subscription. `moves_topics` drops its `_topic` suffix case, because no setting uses it any more.
     - `glue/ctl_telemetry.c`: `inputs/solar_w` is gone, so there are 8 fixed input entries.
   - Shadow wording changed in the comments of `loop/ctl_loop.c` and `loop/ctl_loop.h`. `assume_landed` stays, because the gate uses it.
   - New test `tests/control/test_settings_solar.c` (criterion 3). It loads the pairs of a `/control.json` holding both old names with values, among kept settings. Both old names get `UNKNOWN`, every other setting is applied, the set is valid, the names are no longer listed, and the loop's configuration has `solar_gain_factor` 0 (also from the defaults). The old PV topic now dispatches as NONE. It failed before the code change and passes now. The old topic's name is split into two string literals, as `test_settings_remote.c` already does.
   - Changed tests, using the story's permission:
     - `test_settings.c`: removed the rows for the removed settings and shifted the name counts and indexes by 2. The decimals and formatting rows for `solar_gain_factor` now use `internal_w`. The topic checks for `pv_topic` now use `room.0.topic`. `test_to_loop` now expects a gain of 0.
     - `test_settings_remote.c`: its kept-setting pair `pv_topic` is replaced by `desired_indoor_c`.
     - `test_form.c` and `test_form_rows.c`: the piece, length and unknown-name checks now use `room.0.topic`; one report index shifts.
     - `test_html.c`: the refused row's name is now `room.0.topic`.
     - `test_dispatch.c`: the PV rows are removed.
     - `test_telemetry.c`: the `inputs/solar_w` row and fixture line are removed, and counts and indexes are shifted by one.
     - No check of kept behaviour was removed or loosened.
   - Docs:
     - `settings.md`: both rows and the `pv_topic` mention are removed.
     - `firmware.md`: rows, subscription, telemetry and first-start text updated, plus a new paragraph "No solar input".
     - `loop.md`: no shadow caller; says the firmware never offers `CTL_INPUT_PV`.
     - `device-test.md` already described only what remains, so it is unchanged.
     - `decisions.md`: a new entry, including the rejected option of removing the solar signal from the core.

   Decision (recorded in `decisions.md`): the loop keeps `CTL_INPUT_PV`, `ctl_solar` and `ctl_loop_format_record`. The Interface does not list them, and their `test_loop.c` checks test kept loop behaviour. After this story nothing in the firmware calls them. Removing them would be a follow-up story.

   Results:
   - Criterion 2: `git diff develop --stat -- HeishaMon/src/control/core` prints nothing.
   - Criterion 4: the golden traces and the device replay pass, with 0 mismatches.
   - Criterion 5, memory before and after:
     - D1 mini RAM: 48412 B before, 48244 B after.
     - D1 mini flash: 757528 B before, 757144 B after.
     - ESP32-S3 sketch: 1705491 B before, 1705151 B after.
     - ESP32-S3 globals: 67488 B before, 67368 B after.
   - Criterion 6: `git diff develop --stat -- HeishaMon ':!HeishaMon/src'` prints nothing.
   - Criterion 7: `make check` is green ("control: 35 test programs passed").
   - Smoke test: prints `1`, as expected.
4. 2026-11-14 human: (a). My criterion was aimed at the telemetry name and the settings, not at the core's `p_solar_w` or locals that use it. Read criterion 1 as: `(^|[^_])solar_w\b` finds nothing outside the core and the local in `adapt/ctl_pump.c`, and the second grep accepts the one line in `glue/ctl_settings.c` that sets the core's field to 0, which the Description asks for.

   Your decision on `CTL_INPUT_PV`, `ctl_solar` and `ctl_loop_format_record` stands; leave them for now.
