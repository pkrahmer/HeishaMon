# The target temperature from a thermostat

## Goal

Today the house's target temperature is the setting `desired_indoor_c`, changed on the board's control page or over MQTT. The owner wants anyone in the house to change it on a room thermostat instead: the Homematic thermostats already publish their setpoints over the CCU's MQTT bridge, retained and on change, in the same JSON form as the room temperatures the board already reads. This feature adds one topic setting: when it is set and a valid setpoint has arrived, that setpoint is the controller's target; when it is empty, or no valid setpoint is there, `desired_indoor_c` applies as today. Done when the story is merged, `make check` is green, and the telemetry shows which target is in use.

## Scope

- A setting `desired_topic` and the subscription to it.
- The rule which target applies, and its telemetry.

## Out of scope

- Writing a setpoint back to the thermostat.
- Schedules: the thermostat may run one; the board only follows the setpoint it publishes.
- Any change to the integral term (it keeps learning on a setpoint change, by the owner's choice).

## Stories

- S0001: The target from a thermostat's setpoint

## The human's changes

None. The write paths were checked: the story stays in `HeishaMon/src/control/`, `tests/control/` and `docs/control/`.

## Decisions taken for the human

1. A setpoint counts only between 16 and 26 °C, both included. Anything else (off, frost protection, boost) is ignored, and the controller falls back to `desired_indoor_c`. Reason: Homematic thermostats signal "off" with a setpoint of about 5 °C.
2. A valid setpoint never goes stale: the CCU publishes setpoints retained and only on change, so the last one holds until the next. It is not stored on the board; after a restart the broker delivers it again.
3. The integral term keeps learning on a setpoint change. The owner chose this; a hold after a change was offered and declined.
4. The owner will use the living room thermostat; the feature takes any topic.
