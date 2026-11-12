# The router while commands are blocked

## Goal

On the owner's spare D1, which shadows the house with commands disabled, `outputs/router` says `FAULT`. The router sent its first state, but the glue only counts the emulated frame as a confirmation while that frame goes out to a pump, and on a blocked board it never does. So the router repeated and then gave up. That fault is not real, and it hides a real one. This feature makes a blocked board's router behave as the PC shadow did: every command counts as landed. The glue also publishes why commands are blocked, so `outputs/router` shows only real faults and the owner sees at a glance why nothing is sent. Done when the story is merged and `make check` is green.

## Scope

- While commands are blocked, the loop's router is stepped with "assumed landed", as the PC shadow does.
- A new telemetry entry `outputs/blocked`: `-` when commands may go to the pump, or the first reason they may not.

## Out of scope

- Any change to what reaches the pump when commands are allowed.

## Stories

- S0001: Assumed landing and the reason while blocked

## The human's changes

None.

## Decisions taken for the human

1. A blocked board treats every command as landed, so its router shows `IDLE` or `WAITING` and never a fault it cannot have.
2. The reasons, in the order checked: `disabled` (setting `enabled` is 0), `listen_only`, `no_optional_pcb`.
