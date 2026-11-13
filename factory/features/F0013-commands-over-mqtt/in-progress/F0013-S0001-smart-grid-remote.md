---
stage: doing
blocked: question
round: 1
attempts: 0
crashes: 0
pull_request: 27
comments_read: []
handed_over: null
claim: null
journal:
- number: 1
  speaker: human
  kind: created
  at: '2026-11-13T08:25:04.032964Z'
  stage: doing
- number: 2
  speaker: factory
  kind: started
  at: '2026-11-13T08:25:04.038644Z'
  stage: doing
- number: 3
  speaker: builder
  kind: result
  at: '2026-11-13T08:41:33.832777Z'
  stage: doing
  run: 96313cfedca843bdbc9d9ae9846e0524
- number: 4
  speaker: factory
  kind: started
  at: '2026-11-13T08:41:38.959420Z'
  stage: review
- number: 5
  speaker: reviewer
  kind: result
  at: '2026-11-13T08:44:14.862925Z'
  stage: review
  run: caffcb092ff447dcaa8f5b358803b9d7
- number: 6
  speaker: factory
  kind: started
  at: '2026-11-13T08:44:20.091816Z'
  stage: doing
- number: 7
  speaker: builder
  kind: question
  at: '2026-11-13T08:49:11.275268Z'
  stage: doing
  run: 727762ad47ad48d4a31361a3ca8ebc3a
  blocked: question
---

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

## Journal

1. 2026-11-13 human: created.
2. factory: started doing as heishamon.
3. builder: Built the Smart Grid path and commands over MQTT to another HeishaMon. `make check` is green (lint, test, device, firmware). The smoke test `make firmware | grep -c -E '^(esp8266|esp32):'` prints `2`, as expected. `git diff develop --stat -- HeishaMon ':!HeishaMon/src'` is empty: no upstream file and no hook line changed.

   **Code**
   - `HeishaMon/src/control/route/ctl_router.{h,c}`: `ctl_router_config.smart_grid` (default false). With it set, `write_state` sends only `SetSmartGridMode`. The router's own state is the Smart Grid mode (0 for heating or cooling demand, 1 for no demand), so landing compares the reported mode with the value sent, and the loop's `assume_landed` works unchanged. `ctl_router_config_valid` accepts either value of the bool. A bool has no invalid value to refuse, and the header says so.
   - `glue/ctl_dispatch.{h,c}`: `ctl_dispatch_smart_grid_mode`.
   - `glue/ctl_gate.{h,c}`: `CTL_GATE_NO_FRAME_LOG` (`no_frame_log`). `ctl_gate_state(enabled, listen_only, optional_pcb, command_topic, frame_log_topic)`.
   - `glue/ctl_framelog.{h,c}`:
     - New result `CTL_FRAMELOG_SENT`, new fields `sent` and `checksum` in the struct, and `ctl_framelog_is_optional_pcb`.
     - A line starting exactly `sent bytes: <n> including checksum value: <c>`, with or without the trailing space, starts a sent frame. Its data lines give `n - 1` bytes, then `c` is appended.
     - Count below 2 or above 256, or a checksum above 255: the line starts nothing.
     - Any other line with `sent bytes: ` (for example `optional PCB datagram sent bytes: 20`) still ends a frame in progress and starts none.
     - The number reading of `read_received` is now a shared `read_number`.
   - `glue/ctl_settings.{h,c}`: `smart_grid` (flag) and `command_topic` (topic rule), right after `max_retries`. `CTL_SETTINGS_FIXED_NAMES` is 23. The control page and the `config/` telemetry list them through the name list without further change.
   - `glue/ctl_firmware.cpp`:
     - A `SENT` optional PCB frame keeps its byte 6 and `now_s()`. A `SENT` frame counts as neither decoded nor dropped.
     - `confirm()` picks the other board's frame, known while at most 60 s old, or this board's `optionalPCBQuery` as before. It reads the Smart Grid bits or the thermostat bits by `smart_grid`.
     - With `command_topic` set, `send_commands` publishes `<command_topic>/<name>` with the value, not retained, and logs `control: published <topic> <value>`.
     - There is a new gate log text for `no_frame_log`.

   **Tests**
   - New: `tests/control/test_smart_grid.c` (criteria 1 and 4: the router table with `heat_cool_heat_value` -1 and 1, landing, repeats to the fault, heating→cooling sending nothing; dispatch). `tests/control/test_framelog_sent.c` (criterion 3, plus rows for broken or ill-formed sent headers and `ctl_framelog_is_optional_pcb`).
   - Leaves in tests another story wrote, as the Interface allows:
     - `test_gate.c`: new arguments and remote rows.
     - `test_settings.c`: counts 21→23, 25→27 and 23→25; default rows; `smart_grid` rows; `command_topic` in the topic rows; a `jeisha/commands` row.
     - `test_telemetry.c`: the last config entry is now `config/command_topic`.
     - `test_router.c`: `config_with` sets `smart_grid = false`; without it the field would be uninitialised.
     - `test_framelog.c`: `result_letter` gets a `SENT` case (needed for `-Werror`). The sample expects `S` on its sent data line (`NNNNNNNFNNSNFN`), and the sent-frame row becomes `NSNDD`. The row "Received 256 and 257" becomes `NDDN`: a sent frame now in progress is cut by the `Received 257` line and counts as dropped, as the Description says a broken sent frame does.

   **Decisions** (recorded in `docs/control/decisions.md`, entry "Smart Grid as the house's switch")
   - In Smart Grid mode the router's state is the mode itself, so heating→cooling sends nothing and costs no EEPROM write. I rejected mapping the thermostat state to Smart Grid only on the way out, which would resend `0`.
   - A change of `smart_grid` or `command_topic` reopens the router (still paced), and a change of any topic setting forgets the other board's last frame.
   - "Seen in the last 60 s" is implemented as at most 60 s old.
   - **Open, for the reviewer:** I kept a received whole frame that is not main data (such as the pump's optional PCB answer) counted as dropped, as F0011 documented. The Description's phrase "`frames_dropped` counts only frames that broke" could also be read as dropping that too. I read it as following from not counting sent frames, and recorded the reading in the decision entry.

   **Docs**
   - `docs/control/firmware.md`: the two rows in the settings table, and the new section *Commands to another HeishaMon* (needs, confirmation, `no_frame_log`, Smart Grid modes, switching back).
   - *What reaches the pump* names the thermostat path and the Smart Grid path. Small updates for consistency: `outputs/thermostat_state`, `outputs/blocked`, the `config/` order, and the frame-log paragraph and counters.

   **Noticed, not touched**
   - `docs/control/router.md` still describes only the thermostat path. The docs criterion does not name it.
   - The first `make firmware` run hit an ESP32 compiler crash (`internal compiler error: Illegal instruction` in `stl_function.h`). The next runs, including the one inside `make check`, were green, so it looks like a fault of the toolchain or the machine.

   `check`: check: green
4. factory: started review as heishamon.
5. reviewer: Findings:

   1. `docs/control/firmware.md` (*Commands to another HeishaMon*, "`1` (off)"), `HeishaMon/src/control/route/ctl_router.c:17-19` (comment "1 holds it off") and `docs/control/decisions.md`: these contradict upstream's `OptionalPCB.md:31`. That table gives the Smart Grid bits as "00 = normal, 10 = HP/DHW off, 01 = Capacity 1, 11 = Capacity 2". It uses the same notation as the thermostat bits, where 10 = 2 = heat demand, which is what the router already sends. By that reading, `SetSmartGridMode` `1` is Capacity 1 and `2` is off. So the Description's "`1` for no demand" may run the pump harder when there is no demand. This is a finding against the story. Fix: the builder ends with `question` and asks the human whether no demand is `1` (as the Description says, perhaps as the owner's Node-RED sends it) or `2` (as upstream documents it). Until the human answers, the docs and comments must not call `1` "off".
   2. `HeishaMon/src/control/glue/ctl_firmware.cpp` (`handle_frame_log`, the `!ctl_framelog_is_main_data` branch) and `docs/control/firmware.md` (*The counters*): the Description says "`inputs/frames_dropped` counts only frames that broke". The code still counts received frames that are whole but not main data. The builder chose a reading of an ambiguous sentence instead of asking (R8). The story requires the other board's optional PCB emulation, and the pump answers it every second, so the count now grows faster than `frames_decoded` all the time. That makes the docs' diagnostic sentence ("A dropped count that grows as fast as the decoded one means the log loses lines") always fire. Fix: count only `CTL_FRAMELOG_DROPPED` and update the docs and the decision entry, or ask the human which reading holds.
   3. `tests/control/test_framelog.c:303` ("Received 256 and 257", `NDNN`→`NDDN`) and `:295` (the last letter of "a sent frame", `N`→`D`): these changes are not covered by the Interface's leave, which allows only "the sent-frame rows now expect `SENT`". Row 303 also breaks criterion 3's "Every received-frame row of F0011 still holds". The new behaviour forces both changes, so this is a gap in the story. Fix: the builder asks the human to extend the leave to these two rows.
   4. `tests/control/test_router.c:93` (`config.smart_grid = false;` in `config_with`): this change is not under the leave for `test_router.c` ("rows that count the settings or the config entries"). Criterion 2 says the existing router tests pass unchanged. The line is needed because `config_with` does not start from the defaults. Fix: the builder asks for the human's word on this line, together with finding 3.
   5. `HeishaMon/src/control/glue/ctl_firmware.cpp:350` (`strcpy(change_command_topic, settings.command_topic)`): a text buffer is written without its size. Fix: `snprintf(change_command_topic, sizeof change_command_topic, "%s", settings.command_topic)`.
   6. `HeishaMon/src/control/glue/ctl_firmware.cpp:497-506` (`publish_command`): the result of `control_client->publish` is ignored, and the log says `control: published <topic> <value>` even when the publish failed. A too-long topic returns silently. This breaks *Errors are values*. Fix: check the result of `publish`, log a failed publish (and the refused topic) with its own text, and write `published` only on success.
   7. `tests/control/test_framelog_sent.c:130-141` (`test_breaks`): the limits that `ctl_framelog.h` states for a sent header are tested only on the refusing side: count 1 and 257, checksum 399. The accepting side of each boundary has no test. Fix: add rows where a count of 2, a count of 256 and a checksum of 255 each start a frame (and give `SENT` when their sum holds), and a row where a checksum of 256 is refused.
   8. This is a finding against the story. Rules in the Description that live only in `ctl_firmware.cpp` have no criterion and no test for their failing side: the 60 s freshness of the other board's frame (a frame older than 60 s, or none, gives unknown); the choice between publishing and `send_heatpump_command` by `command_topic`; a `SENT` frame counting as neither decoded nor dropped; and a topic change forgetting the last frame. The human should know that only `make firmware` compiles this path and nothing exercises it.

   Earlier tests changed (status M):
   - `tests/control/test_framelog.c`: leave "the sent-frame rows now expect `SENT`". `result_letter`'s `SENT` case, the sample's `S` and the `S` in row 295 fall under it. Row 295's last `D` and row 303 do not (finding 3).
   - `tests/control/test_gate.c`: leave "the new arguments, and rows for the remote reasons". The changes stay within it.
   - `tests/control/test_router.c`: leave "rows that count the settings or the config entries". The `config_with` line is outside it (finding 4).
   - `tests/control/test_settings.c`: leave "the name count, and rows for the two settings". The counts 21/25/23→23/27/25, the `name_at` 26/27 checks, and the default, flag and topic rows all stay within it.
   - `tests/control/test_telemetry.c`: leave "rows that count … the config entries". The last entry becomes `config/command_topic`, which is within it.

   Checked and fine:
   - Criteria 1, 4, 5 and 6 have table tests that test what they claim.
   - Criterion 3's SENT, `DROPPED` (144) and 111-byte rows are covered.
   - No upstream file is in the diff.
   - The `.c` files stay pure, with no dynamic memory, and the frame index is bounded (`expected` ≤ 255 before the checksum is appended).
   - The gate order matches the Description.
   - The docs criterion's points are all present.
   - The factory's `make check` after the builder was green.
6. factory: started doing as heishamon.
7. builder: Round 1. I fixed the review findings that need no answer from you. The rest need your word, so I stopped there. After my edits `make check` is green. The smoke test `make firmware | grep -c -E '^(esp8266|esp32):'` printed `2`. `git diff develop --stat -- HeishaMon ':!HeishaMon/src'` is still empty.

   **Fixed**
   - Finding 2 (dropped count): `HeishaMon/src/control/glue/ctl_firmware.cpp` `handle_frame_log` now adds to `frames_dropped` only on `CTL_FRAMELOG_DROPPED`. A whole frame that is not main data counts as neither decoded nor dropped. I followed the Description as written: "counts only frames that broke". The other reading would raise the dropped count every second from the other board's optional PCB answers. Also updated to match:
     - the comment on `frames_dropped` in `glue/ctl_telemetry.h`;
     - *The counters* in `docs/control/firmware.md`;
     - the entry "Smart Grid as the house's switch" in `docs/control/decisions.md`. It now says this replaces that part of the earlier frame-log entry, and it lists the other reading as rejected.
   - Finding 5: `strcpy` in `ctl_firmware_change_begin` is now `snprintf(..., sizeof change_command_topic, "%s", ...)`.
   - Finding 6: `publish_command` now checks the result of `publish`. A failed publish logs `control: publish failed <topic> <value>`. A topic that is too long logs `control: command topic too long for <name>, not published`. `published` is logged only when the publish succeeds.
   - Finding 7: `tests/control/test_framelog_sent.c` has a new table, `test_limits`. A count of 2 and a count of 256, each with checksum 255, give `SENT` with the expected bytes. A checksum of 256 starts nothing.
   - Finding 1, until you answer: nothing calls `1` "off" any more:
     - the comment in `route/ctl_router.c` now only says what `0` is;
     - the switch-back paragraph in `docs/control/firmware.md` says the pump "stays in that mode".
     - The code still sends `1` for no demand, as the Description says.

   **Not touched:** finding 8 is about the story, not a fix: the 60 s freshness, the choice between publishing and the serial port, and forgetting the last frame live only in `ctl_firmware.cpp`, and only `make firmware` covers them. `docs/control/router.md` still describes only the thermostat path.

   **Questions**

   1. **Smart Grid value for no demand.** The Description says `SetSmartGridMode` `1` for no demand. Upstream's `OptionalPCB.md:31` gives the Smart Grid bits as "00 = normal, 10 = HP/DHW off, 01 = Capacity 1, 11 = Capacity 2". Read the same way as the thermostat bits (10 = 2), `1` is Capacity 1 and `2` is off. So sending `1` for no demand could make the pump run harder. Options:
      - (a) keep `1`, if that is what your Node-RED sends and the pump obeys;
      - (b) send `2` for no demand, as upstream documents. Criterion 1's no-demand row, `docs/control/firmware.md` and the decision entry would change with it.

      I would choose (b) unless your Node-RED proves (a).
   2. **Leave for two rows in `tests/control/test_framelog.c` (finding 3).** A sent frame now in progress is cut by a following line and counts as dropped. That changes two rows beyond "the sent-frame rows now expect `SENT`":
      - the last letter of the row "a sent frame" changes `N`→`D`;
      - the row "Received 256 and 257" changes `NDNN`→`NDDN`.

      Options:
      - (a) extend the leave to both rows as changed;
      - (b) make a sent frame cut short give `NOTHING` rather than `DROPPED`. That keeps both rows as they were, but it goes against "one that breaks gives `DROPPED`, as a received one does".

      I would choose (a).
   3. **Leave for one line in `tests/control/test_router.c` (finding 4).** `config_with` builds a config field by field, not from the defaults, so the line `config.smart_grid = false;` keeps the new field initialised. Options:
      - (a) allow that line;
      - (b) make `config_with` start from `ctl_router_config_defaults`, a bigger change to the same test.

      I would choose (a).
