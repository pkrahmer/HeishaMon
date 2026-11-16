# Set the energy balance over MQTT

## Goal

Every restart of the board resets the energy balance to 0, so the pump rests until the balance reaches the start threshold again, although the house's state has not changed. The owner wants to put the balance back after a restart, for example after a crash or a test restart while diagnosing one, so that the pump continues where it was. The value before the restart is in the owner's InfluxDB. This feature adds a one-shot command that sets the balance. Done when the story is merged and `make check` is green.

## Scope

- A command topic `<base>/control/set/energy_wh` that sets the energy balance once.

## Out of scope

- Restoring the balance automatically at boot.
- Storing the balance on the board.

## Stories

- S0001: The energy balance command

## The human's changes

None. The write paths were checked: the story stays in `HeishaMon/src/control/`, `tests/control/` and `docs/control/`.

## Decisions taken for the human

1. The value is clamped to the controller's own anti-windup bounds for the directions allowed at that moment, so a command can do no more than the balance could reach on its own.
2. The command is no setting: it is not stored and not shown as one, like `integral_reset`.
