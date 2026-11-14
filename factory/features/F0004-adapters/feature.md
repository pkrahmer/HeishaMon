# The adapters: from MQTT payloads and pump values to the core's five signals

## Goal

The core takes five signals; the house delivers MQTT payloads and decoded pump values. The adapters close that gap, as pure C under `HeishaMon/src/control/adapt/`. They parse a payload, refuse what is implausible, average the room thermostats into one indoor temperature, mask the produced heat while the pump makes hot water or defrosts, and scale the PV power into a solar gain. Each adapter takes values and instants and returns readings for the core; none of them reads a topic, a clock or a file. They are for the owner, who needs them for the live shadow on the D1 and later inside the firmware (`docs/control/firmware-design.md` §3 and §3.1, `docs/control/algorithm-plan.md` §2.5 and [A-3]). Done when every story below is merged and `make check` is green.

## Scope

- A payload parser: a bare number, or a JSON object with a value field and an optional timestamp.
- Plausibility bounds, one place for all of them.
- The room average: up to 16 rooms with weights, a per-room timeout, the more-than-half-of-the-weight rule, and a refusal of replayed payloads.
- The pump signals: the outdoor temperature, the produced heat masked to 0 W during hot water and defrost, and the PV power scaled by `solar_gain_factor`.
- `docs/control/adapters.md`.

## Out of scope

- Topics, the MQTT client, the clock and the configuration file: the glue of a later feature, or the shadow script.
- The internal gains: the core's configured constant stays (`P_INTERNAL` 550 W).
- The router: the next feature.

## Stories

The build order; start one at a time, the next when the one before is archived in `done/`.

- S0001: The payload parser and the plausibility bounds
- S0002: The room average
- S0003: The pump signals and the solar gain

## The human's changes

None. Each story writes only under the builder's write paths; `make lint` and `make test` already cover every `.c` file under `HeishaMon/src/control/`, and `make device` takes `adapt/` as one more library folder.

## Decisions taken for the human

1. The room timeout is a setting, `room_timeout_s`, with the design's 1800 s as its default. The broker showed CCU3 room values up to about 90 minutes old on 2026-11-11, so the shadow measures the real cadence before the default is reconsidered.
2. A payload whose timestamp does not advance over the room's last one is a replay (a retained message after a reconnect) and does not refresh the room.
3. Defrost masks the produced heat like hot water, as [A-3] says.
4. A negative produced heat or PV power is implausible and dropped, not clamped to 0.
5. Every adapter follows the core's conventions: `double`, `ctl_` names, no dynamic memory, errors as values.
