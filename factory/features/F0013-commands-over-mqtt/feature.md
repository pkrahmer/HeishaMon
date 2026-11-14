# Commands to another HeishaMon, by Smart Grid mode

## Goal

The owner wants the control loop to run the house's heat pump for a whole day, from the dev board, while the production HeishaMon stays as it is. Two things stand in the way. First, the owner's pump ignores the optional PCB's external thermostat input: production sends thermostat 1 as "no demand" (byte 6 is `0x40`) and the pump heats anyway. The owner's Node-RED switches the pump by Smart Grid mode instead, which the pump obeys: mode 0 allows heating, mode 1 stops heating and cooling and keeps hot water at 100%. Second, the dev board's commands can only go to its own serial port, and it has no pump.

This feature lets the router speak Smart Grid mode, and lets the glue publish the router's commands to another HeishaMon's command topics (`<base>/commands/<name>`, upstream's own command interface) instead of its own serial port. The other board's log, already read for its frames, also carries the optional PCB frames it sends to the pump; the glue reads the Smart Grid bits from them, so the router still confirms that each command landed. Done when the story is merged, `make check` is green, and the dev board switches production's Smart Grid mode on its demand.

## Scope

- A setting `smart_grid`: the router sends `SetSmartGridMode` instead of the thermostat and heat/cool commands.
- A setting `command_topic`: the commands go to MQTT instead of the serial port.
- The frame log parser also completes the frames the other board sends; the glue reads the other board's optional PCB byte 6 from them.
- The gate's reasons for a board that commands another one.

## Out of scope

- The external thermostat path stays as it is, the default.
- The cleanup of the four pump topics of F0009 (later, by the owner's word).
- Any change to the production board's firmware.

## Stories

- S0001: Smart Grid commands to another HeishaMon

## The human's changes

On the dev board, after the merge and a flash: `smart_grid` 1, `command_topic` `jeisha/commands`, then `enabled` 1. Node-RED stays stopped while the dev board runs the pump.

## Decisions taken for the human

1. Smart Grid values: heating or cooling demand sends 0, no demand sends 1. Mode 2 (everything off) and 3 (hot water boost) are never sent.
2. A remote board's command counts as landed only when the other board's next sent optional PCB frame shows it; without such a frame in the last 60 s, the state is unknown and the router retries as it does today.
3. With `command_topic` set, the gate ignores this board's listen-only mode and optional PCB setting (they concern its own serial port) and needs `frame_log_topic` instead.
