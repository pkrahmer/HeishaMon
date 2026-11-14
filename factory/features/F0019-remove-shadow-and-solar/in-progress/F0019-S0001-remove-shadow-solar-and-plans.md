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
