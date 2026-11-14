# Integral correction

## Goal

The controller corrects the indoor temperature only in proportion to its error: `adjustment_w_per_k` adds power per kelvin the indoor average is off its target. Whatever the model gets wrong (the house's real heat loss, the internal gains, wind, sun) therefore leaves a steady offset, the model's error divided by that gain. The owner has always seen the house a little too warm or a little too cold; on the first night under the fork's own control it stayed about 0.4 K above 22 °C with the gain at 300 W/K, a model error of about 120 W. This feature adds a slow integral term that learns that missing power and removes the offset, capped and protected against windup, and keeps what it learned across a restart. It is off by default: a board that is flashed without changing a setting behaves exactly as before. Done when both stories are merged and `make check` is green.

## Scope

- An integral term in the energy balance, its two settings, its telemetry and its share in the ledger.
- Keeping the learned integral across a restart, with few flash writes.

## Out of scope

- The Python reference and the golden traces: the term is the fork's own; with the term off, every trace replays bit for bit as today.
- Any change to the proportional term, the hysteresis or the router.
- A derivative term.
- Tuning: the owner chooses the gain on the board.

## Stories

- S0001: The integral term
- S0002: The integral survives a restart

## The human's changes

None. The write paths were checked: both stories stay in `HeishaMon/src/control/`, `tests/control/` and `docs/control/`.

## Decisions taken for the human

1. The term is off by default (`integral_w_per_kh` 0), so the flash changes nothing until the owner switches it on. Reason: production runs the pump; a new behaviour starts only by the owner's hand.
2. The integral is capped at ±`integral_limit_w`, default 300 W, about twice the model error seen on the first night. Reason: a wrong integral can never push harder than the cap.
3. The integral learns only while it can be trusted: every required input fresh, the indoor error inside the trim limit, and the energy not clamped at its bound. Reason: anti-windup; the floor heating answers in hours, and an integral that keeps learning while the pump cannot act builds an overshoot.
4. The learned integral is kept in its own small file, written at most every 6 hours and only when it moved by 10 W or more since the last write, and never in `/control.json`. Reason: the owner cares about flash wear; this is at most four small writes a day. A broker-retained copy was rejected: the broker may be down when the board boots.
5. A start value for the owner, not a default: `integral_w_per_kh` 25, an integral time of about a day at `adjustment_w_per_k` 600. Written in `docs/control/settings.md`.
