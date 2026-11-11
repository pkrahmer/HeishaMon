# The router: from the core's demand to the pump's thermostat commands

## Goal

The core decides heating, cooling or no demand; the pump is driven through the thermostat-demand input of HeishaMon's optional PCB emulation (`docs/control/firmware-design.md` §4, decision F-4). The router closes that gap, as pure C under `HeishaMon/src/control/route/`: it maps a demand to the commands `SetExternalThermostat1State` and `SetHeatCoolMode`, sends only on a change, paces changes so that the pump's EEPROM is not worn, checks that a command landed and repeats it when it did not, and reports a router fault after the repeats run out. It only builds command names and values; it sends nothing. It is for the owner, who needs it for the live shadow on the D1 and later inside the firmware. Done when the story is merged and `make check` is green.

## Scope

- The mapping from demand to commands, with the polarity of `SetHeatCoolMode` as a setting.
- Send on change, the pacing of changes, the confirmation and the repeats.
- `docs/control/router.md`.

## Out of scope

- Sending a command: the glue of a later feature calls HeishaMon's command function, or the shadow only logs.
- Where the confirmation comes from: the glue reads the pump's reported state and hands it to the router.
- Hot water and SmartGrid: not this loop's business (§4 of the design).

## Stories

- S0001: The router

## The human's changes

None.

## Decisions taken for the human

1. The polarity of `SetHeatCoolMode` is not known (`docs/control/firmware-design.md` §9, item 3). It is a setting without a default; while it is unset, the router sends no heat/cool command at all, only the thermostat state. With cooling masked off for the first winter, heating needs no heat/cool command as long as the pump stays in heat mode.
2. The confirmation is an input: the glue reports the thermostat state the pump shows, or that it does not know it. A state the pump never reports leaves the router waiting, then repeating, then in fault, which is visible in the telemetry.
3. The router paces changes of the thermostat state to at most one per `min_change_interval_s` (default 300 s). A demand that changes faster is held back and sent when the interval has passed, if it still holds. The core's hysteresis keeps changes near one per hour, so the pacing only bites on a fault.
