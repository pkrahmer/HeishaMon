# Smart Grid commands to another HeishaMon

## Description

**Why.** The owner's pump ignores the external thermostat input of the optional PCB; it obeys Smart Grid mode, which the owner's Node-RED uses today. The owner's dev board has no pump of its own: it decodes the production board's frames from that board's MQTT log (`frame_log_topic`, F0011). This story lets the dev board run the house's pump through the production board.

**Smart Grid mode in the router.** A new setting `smart_grid` (0 or 1, default 0). With 0, the router behaves exactly as today. With 1, a state change sends one command, `SetSmartGridMode`, with `0` for heating or cooling demand and `1` for no demand, and never `SetExternalThermostat1State` or `SetHeatCoolMode`. Pacing, retries, the fault and the reopen work as today. A command has landed when the reported value equals the Smart Grid value sent. In upstream's optional PCB byte 6, Smart Grid mode is bits 4 and 5 (`(byte >> 4) & 3`), as `set_smart_grid_mode` in `HeishaMon/commands.cpp` writes it.

**Commands over MQTT.** A new setting `command_topic` (the topic rule, empty by default). When it is set, the glue publishes each command of the router as one MQTT message, topic `<command_topic>/<name>` and payload the value, not retained, instead of calling `send_heatpump_command`. Upstream HeishaMon subscribes to `<base>/commands/#` and executes such a message as if it came from its own web page. When it is empty, commands go to the serial port as today.

**The other board's sent frames.** HeishaMon logs a frame it sends like this, with the checksum not in the hex dump but as a decimal number in the header line (taken from the owner's production board):

```
Fri Nov 13 09:22:04 2026 (125253578): sent bytes: 20 including checksum value: 143 
Fri Nov 13 09:22:04 2026 (125253579): data: F1 11 01 50 00 00 40 FF FF E5 FF FF 00 FF 00 FF FF 00 00 
```

The parser now completes such a frame too: `sent bytes: <n> including checksum value: <c>` starts a sent frame of `n` bytes; the data lines give the first `n - 1` bytes, and the parser appends `c` as the last byte. A sent frame that is complete and whose checksum holds gives a new result `CTL_FRAMELOG_SENT`; one that breaks gives `DROPPED`, as a received one does. A sent frame is an optional PCB frame when it has 20 bytes, byte 0 is `0xF1` and byte 3 is `0x50`. For each such frame, the glue keeps its byte 6 and the time it saw it. The counters: a `SENT` frame counts as neither decoded nor dropped, so `inputs/frames_dropped` counts only frames that broke.

**What the router is told.** With `command_topic` set, the state is known when an optional PCB frame of the other board was seen in the last 60 s, and the reported value is read from that frame's byte 6. Otherwise, as today, from this board's own `optionalPCBQuery[6]`, known when the optional PCB is on and the board is not listen-only. In both cases, the reported value is the Smart Grid bits when `smart_grid` is 1 and the thermostat bits when it is 0.

**The gate.** With `command_topic` empty, the gate is as today. With it set, the reasons in order: `disabled` (`enabled` 0), `no_frame_log` (`frame_log_topic` empty, so nothing could confirm a command), otherwise open. Listen-only and the optional PCB setting of this board do not apply: they concern its own serial port.

## Interface

- Settings: `smart_grid` (a whole number 0 or 1, default 0) and `command_topic` (the topic rule, default empty), in the name order right after `max_retries`. The name count with `rooms` 0 becomes 23. Both are on the control page with the other settings, and in the `config` telemetry like every setting.
- `ctl_framelog_result` gains `CTL_FRAMELOG_SENT`. A new pure function, in `ctl_framelog.h`: `bool ctl_framelog_is_optional_pcb(const uint8_t *frame, size_t length)`, true for a 20-byte frame with `0xF1` at byte 0 and `0x50` at byte 3, false for any other frame and for NULL.
- `ctl_dispatch.h` gains `int ctl_dispatch_smart_grid_mode(uint8_t pcb_byte_6)`, returning `(byte >> 4) & 3`.
- `ctl_gate` gains `CTL_GATE_NO_FRAME_LOG`, named `no_frame_log`. `ctl_gate_state` takes, in addition to what it takes today, whether `command_topic` is set and whether `frame_log_topic` is set, and returns the reasons in the order of the Description.
- `ctl_router_config` gains `bool smart_grid` (default false), checked by `ctl_router_config_valid` like every field.
- The leaves for tests another story wrote: `tests/control/test_settings.c` (the name count, and rows for the two settings), `tests/control/test_gate.c` (the new arguments, and rows for the remote reasons), `tests/control/test_framelog.c` (the sent-frame rows now expect `SENT` where criterion 3 says), `tests/control/test_router.c` and `tests/control/test_telemetry.c` (rows that count the settings or the config entries). Nothing else in those files changes. New tests go into new files.
- No upstream file changes, and no hook line changes.

## Acceptance criteria

1. Smart Grid in the router, as rows of one table test with `smart_grid` true and `heat_cool_heat_value` -1 and 1: heating demand sends exactly one command `SetSmartGridMode` `0`; cooling demand sends `SetSmartGridMode` `0`; no demand sends `SetSmartGridMode` `1`; no row sends `SetExternalThermostat1State` or `SetHeatCoolMode`. A reported value equal to the value sent lands the command; a different one leads to the retries and, after `max_retries`, the fault, as with the thermostat.
2. With `smart_grid` false, every existing router test passes unchanged.
3. The parser on the sent frame of the Description followed by a received 20-byte answer: the sent frame gives `SENT` on its data line, with the 20 bytes `F1 11 01 50 00 00 40 FF FF E5 FF FF 00 FF 00 FF FF 00 00 8F`; the answer gives `FRAME`. The same sent frame with checksum value `144` gives `DROPPED`. A sent query of 111 bytes (four data lines of up to 32 bytes) gives `SENT`, and `ctl_framelog_is_optional_pcb` is false for it and true for the 20-byte one. Every received-frame row of F0011 still holds.
4. `ctl_dispatch_smart_grid_mode`: `0x40` gives 0, `0x50` gives 1, `0x60` gives 2, `0x70` gives 3, `0x4C` gives 0.
5. The gate, as rows of one table: without `command_topic` every existing row holds; with it set: not enabled gives `disabled`; enabled without `frame_log_topic` gives `no_frame_log`; enabled with it gives open, also while this board is listen-only and has no optional PCB.
6. The settings: `smart_grid` accepts `0` and `1` and refuses `2`; `command_topic` accepts `jeisha/commands` and the empty text and refuses `a+b`; the name count is 23.
7. `make firmware` compiles both boards, and `git diff develop --stat -- HeishaMon ':!HeishaMon/src'` shows no upstream file changed.
8. `make check` stays green.
9. docs: `docs/control/firmware.md` lists the two settings, and gets a section *Commands to another HeishaMon*. It says:
   - what the dev board needs (`frame_log_topic`, `command_topic`, `smart_grid`, `enabled`), and that the other board must have its optional PCB emulation on;
   - how a command is confirmed, and what `no_frame_log` means;
   - that the pump must obey Smart Grid mode, and which modes are sent;
   - how to switch back: `enabled` 0 on the dev board stops its commands; the other board keeps the last Smart Grid mode it received.
   The section *What reaches the pump* names the Smart Grid path beside the thermostat path. `docs/control/decisions.md` gets one entry: Smart Grid as the house's switch, because the pump ignores the thermostat input.

## Smoke test

```sh
make firmware | grep -c -E '^(esp8266|esp32):'
```

Expect: `2`
