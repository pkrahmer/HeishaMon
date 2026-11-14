# Copy a setting's payload before the change begins

## Goal

On the owner's dev board, setting `frame_log_topic` to `jeisha/log` over MQTT stored `/1/ACTUAL_` instead: ten characters of a room topic, as many as the value. The glue's `handle_setting` calls `ctl_firmware_change_begin` before it copies the payload. For a setting that moves topics, that call unsubscribes, which writes into PubSubClient's single buffer, the buffer the payload still lives in. This feature copies the payload first, and makes the frame log's handler and the input handler equally safe. Done when the story is merged, `make check` is green, and the dev board stores `jeisha/log` when it is set over MQTT.

## Scope

- `handle_setting`: the copy before anything else.
- A review of every path of `ctl_firmware_mqtt`, so that no payload or topic pointer into the client's buffer is read after a call that can send.

## Out of scope

- Upstream's own callback.

## Stories

- S0001: The payload first

## The human's changes

None.

## Decisions taken for the human

1. The glue copies at the entry of `ctl_firmware_mqtt` only what it needs, and only once, rather than copying every message: the input and frame log paths send nothing before they finish reading.
