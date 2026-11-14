# Remove the shadow, the solar input and the planning documents

## Goal

The owner wants the fork to carry only what the production board uses. After F0018 removed the remote-board mode, three things are left that nothing uses: the shadow, which compared the C controller live against the owner's Node-RED (Node-RED is off and the production board runs the loop itself); the solar input, which the owner's house does not need (two small south windows, insulated walls and roof; at a solar gain factor of 0.02 the term is a few dozen watts that the proportional and integral corrections absorb); and two planning documents that `core.md`, `firmware.md` and `decisions.md` have replaced. Done when the story is merged, `make check` is green, and the docs describe only what the firmware and the tools do.

## Scope

- The shadow: `loop/ctl_shadow.*`, the shadow tool under `tests/control/device/`, its tests and `docs/control/shadow.md`.
- The solar input in the glue: the settings `pv_topic` and `solar_gain_factor`, the input `inputs/solar_w`, their telemetry, page rows and docs.
- The documents `docs/control/algorithm-plan.md` and `docs/control/firmware-design.md`.

## Out of scope

- The core's optional solar signal and its configuration field: they stay, so that the golden traces (one is a sunny day) replay bit for bit. The glue never feeds the signal and sets the factor to 0.
- The thermostat path, cooling, the device replay and the integral term.
- Upstream files.

## Stories

- S0001: Remove the shadow, the solar input and the planning documents

## The human's changes

None, unless the shadow tool is named in the `Makefile`; the builder checks and asks before touching it. The write paths were checked otherwise: the story stays in `HeishaMon/src/control/`, `tests/control/` and `docs/control/`.

## Decisions taken for the human

1. The core keeps the solar signal and `solar_gain_factor` as a field; the board sets the factor to 0 and never feeds the signal. Reason: the golden traces are compared bit for bit, and nobody edits them.
2. A `/control.json` that still holds `pv_topic` or `solar_gain_factor` loads without error; the names are ignored.
3. Links from other documents to the removed ones are removed or pointed to `core.md`, `firmware.md` or `decisions.md`.
