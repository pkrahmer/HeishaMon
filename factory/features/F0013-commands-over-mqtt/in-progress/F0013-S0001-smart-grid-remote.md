---
stage: review
blocked: question
round: 2
attempts: 0
crashes: 0
pull_request: 27
comments_read: [6848295711, 6848321203, 6848802245]
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
- number: 8
  speaker: human
  kind: comment
  at: '2026-11-13T08:49:20.925960Z'
  stage: doing
- number: 9
  speaker: human
  kind: comment
  at: '2026-11-13T08:49:20.926096Z'
  stage: doing
- number: 10
  speaker: human
  kind: comment
  at: '2026-11-13T09:11:28.435621Z'
  stage: doing
- number: 11
  speaker: factory
  kind: started
  at: '2026-11-13T09:11:32.178877Z'
  stage: doing
- number: 12
  speaker: builder
  kind: result
  at: '2026-11-13T09:25:03.940655Z'
  stage: doing
  run: e1ec749a59254062baf7c485ce7af5d0
- number: 13
  speaker: factory
  kind: started
  at: '2026-11-13T09:25:09.762367Z'
  stage: review
- number: 14
  speaker: reviewer
  kind: result
  at: '2026-11-13T09:26:33.469180Z'
  stage: review
  run: 9403aef1037042f7b47a2e0256ef8037
- number: 15
  speaker: factory
  kind: started
  at: '2026-11-13T09:26:39.970030Z'
  stage: doing
- number: 16
  speaker: builder
  kind: result
  at: '2026-11-13T09:33:08.938492Z'
  stage: doing
  run: 144af378b05643a8a3dd72206c31220a
- number: 17
  speaker: factory
  kind: started
  at: '2026-11-13T09:33:14.344865Z'
  stage: review
- number: 18
  speaker: reviewer
  kind: question
  at: '2026-11-13T09:34:00.276501Z'
  stage: review
  run: d91b59816b8b447994a3baf6a4a31d47
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
8. 2026-11-13 human: The human's answers to the review findings (part of the Description):

   1. **Smart Grid values stay as the story says: 0 for demand, 1 for no demand.** This is verified on the owner's pump, not taken from a table: the owner's Node-RED sends `SetSmartGridMode` `1` to stop heating (its node "SG Mode 1 (No Heat/Cool, 100% DHW)"), and the pump stopped every time in the recorded history. Upstream's `OptionalPCB.md` most likely writes the two bits in the other order. The docs and the decision entry say: value 1 stops heating and cooling and keeps hot water, verified on the owner's pump by its Node-RED history; upstream's table reads differently, so another pump should be checked first. The code comment says the same in short.
   2. `inputs/frames_dropped` counts only frames that broke. A received frame that is whole and not main data counts as neither decoded nor dropped, like a `SENT` frame. The docs say so.
   3. Leave granted: `tests/control/test_framelog.c` rows 295 and 303 may change as the new behaviour forces.
   4. Leave granted: the line `config.smart_grid = false;` in `config_with` of `tests/control/test_router.c`.
   5. and 6. Fix as the reviewer says.
   7. Add the accepting rows as the reviewer says.
   8. The rules that live only in `ctl_firmware.cpp` need no host test in this story: they are checked on the owner's dev board after the merge. Where such a rule can be a pure function without a new file, the builder may make it one and test it; it is not required.
9. 2026-11-13 human: Correction to answer 1, from the owner (part of the Description):

   Upstream's `OptionalPCB.md` is right: Smart Grid value 1 is "Capacity 1", value 2 is "heat pump and hot water off". The owner configured Capacity 1 on the pump itself to a heating curve that never starts heating, so on this pump value 1 stops room heating and keeps hot water. Value 2 would stop hot water too, which is why it is never sent.

   The code stays as the story says (0 for demand, 1 for no demand). The docs and the decision entry say:
   - value 1 is upstream's "Capacity 1"; it stops room heating only because the owner set Capacity 1 on the pump to a curve that never heats;
   - another pump needs the same installer setting before `smart_grid` is used, or value 1 only lowers its capacity;
   - value 2 is never sent because it stops hot water too.
   The code comment says the same in short. This replaces the reading "upstream writes the bits in the other order".
10. 2026-11-13 human: The human answers the builder's three questions of round 1 (part of the Description): 1. (a), keep 1, as the correction above explains (Capacity 1, set on the owner's pump to a curve that never heats). 2. (a), the two rows of test_framelog.c may change. 3. (a), the one line in test_router.c may change. Everything else as in the two comments above.
11. factory: started doing as heishamon.
12. builder: Round 1, after the human's answers (entries 8–10). The fixes for findings 2, 5, 6 and 7 were already in place from my previous entry. The leaves for `test_framelog.c` rows 295 and 303 and for the `config_with` line in `test_router.c` are now granted, so those changes stay as they were. This round adds only the Smart Grid wording that the owner's correction asks for. The code still sends 0 for demand and 1 for no demand.

    **Changed**
    - `HeishaMon/src/control/route/ctl_router.c` (the comment above `SMART_GRID_DEMAND`): mode 1 is upstream's Capacity 1, and it stops room heating only where the installer set Capacity 1 to a curve that never heats. Mode 2 is never sent because it also stops hot water.
    - `docs/control/firmware.md`, section *Commands to another HeishaMon*, "The pump must obey Smart Grid mode": a new paragraph that says:
      - value `1` is upstream's "Capacity 1";
      - it stops room heating on the owner's pump only because Capacity 1 is set there to a curve that never heats, and hot water goes on;
      - another pump needs the same installer setting first, or value `1` only lowers its capacity;
      - value `2` ("heat pump and hot water off") is never sent because it stops hot water too.
    - `docs/control/firmware.md`, "How to switch back": now says "stays in Capacity 1".
    - `docs/control/firmware.md`, *What reaches the pump*: the Smart Grid path names `1` as Capacity 1, set on the owner's pump to a curve that never heats.
    - `docs/control/decisions.md`, entry "Smart Grid as the house's switch": the Decision gets the Capacity 1 explanation and the note about another pump. Sending `2` for no demand is added under Rejected.
    - Nothing calls `1` "off", and nothing says upstream writes the bits in the other order.

    **Answer 2 (`frames_dropped`)** was already done last round: the code counts only `CTL_FRAMELOG_DROPPED`, and *The counters* in `firmware.md` says a whole frame that is not main data counts as neither decoded nor dropped. No change this round.

    **Answer 8:** I added no host test for the rules that live only in `ctl_firmware.cpp`. The answer says the owner checks them on the dev board after the merge, and it does not require one.

    **Checks**
    - `make check` is green: lint, test, device, firmware, then `check: green`.
    - The smoke test `make firmware | grep -c -E '^(esp8266|esp32):'` printed `2` on two runs in a row, as expected. One earlier run printed `1`: the ESP32 build crashed there.
    - `git diff develop --stat -- HeishaMon ':!HeishaMon/src'` is empty.

    **Noticed, not touched**
    - The ESP32 build sometimes crashes with an internal compiler error. This round it happened three times, in upstream and library files (`NetworkClient.cpp`, `OneWire.cpp`), with 14 GB of memory free, and the next run passed each time. A run that crashes reports `firmware` red, so the factory's own `make check` may also be red for this reason alone. The machine or toolchain is worth a look.
    - `docs/control/router.md` still describes only the thermostat path. The docs criterion does not name it.

    `check`: check: green
13. factory: started review as heishamon.
14. reviewer: Findings:

    1. `docs/control/firmware.md:488` (*Commands to another HeishaMon*, "How a command goes out"): the docs say "Each publish writes the log line `control: published <topic> <value>`". The code no longer does that in every case. `publish_command` in `HeishaMon/src/control/glue/ctl_firmware.cpp` writes `published` only when the publish succeeds. A failed publish logs `control: publish failed <topic> <value>`, and a topic that is too long logs `control: command topic too long for <name>, not published`. Fix: say that `published` is logged only on success, and name the two other log lines and when each one appears.
    2. Work item `## Description`: the human's answers in journal entries 8–10 are marked "part of the Description", but the Description was not rewritten to include them (R5). As it stands, the Description says something other than what was built:
       - The Interface's leave for `tests/control/test_framelog.c` still allows only "the sent-frame rows now expect `SENT`", and it still says "Nothing else in those files changes". It does not name the granted rows 295 ("a sent frame", last letter `D`) and 303 ("Received 256 and 257", `NDDN`), or the granted `config.smart_grid = false;` line in `config_with` of `tests/control/test_router.c`.
       - Criterion 3 still says "Every received-frame row of F0011 still holds", but row 303 changed.
       - Criterion 9 does not ask for the Capacity 1 explanation that the docs and the decision entry now give (value 1 is upstream's Capacity 1, another pump needs the same installer setting, and 2 is never sent).
       - The Description does not say that a whole received frame that is not main data counts as neither decoded nor dropped.
       Fix: fold these answers into the Interface, criterion 3 and criterion 9, keeping the two closing criteria last.

    Checked and fine:
    - Criteria 1 and 4: `tests/control/test_smart_grid.c` covers them. The table has `heat_cool_heat_value` -1 and 1, one `SetSmartGridMode` per send, landing, repeats up to `max_retries` and then the fault, and the five dispatch values.
    - Criterion 3: `tests/control/test_framelog_sent.c` covers the sent frame then the answer, checksum 144 giving `DROPPED`, the 111-byte query over four lines, and `ctl_framelog_is_optional_pcb`. Each header limit is tested on both sides: count 1/2, 256/257, checksum 255/256.
    - Criterion 5: the `test_gate.c` rows hold, including open while listen-only and without an optional PCB.
    - Criterion 6: the `test_settings.c` rows hold: `smart_grid` 0/1 accepted and 2 refused; `command_topic` accepts `jeisha/commands` and empty text and refuses `a+b`; the name count is 23.
    - The factory's `make check` after the builder was green (entry 12).
    - Architecture: no upstream file or hook line is in the diff. The `.c` files stay pure, with no dynamic memory. The sent frame's index is bounded (`expected` ≤ 255 before the checksum byte is appended, frame size 256). Text buffers are written with their size (`snprintf` in `ctl_firmware_change_begin` and `publish_command`). `publish` is checked, and the gate order matches the Description.
    - Answer 8 accepts that the firmware-only rules (60 s freshness, publish versus serial, forgetting the last frame) have no host test.
    - The decision entry gives the main decision and what was rejected. The docs criterion's points are present, including *What reaches the pump* with both paths.

    Earlier tests changed (status M):
    - `tests/control/test_framelog.c`: the `SENT` letter and sample under the Interface's leave; rows 295 and 303 under the human's leave (entries 8 and 10).
    - `tests/control/test_gate.c`: new arguments and remote rows, under the Interface's leave.
    - `tests/control/test_router.c`: the `config_with` line, under the human's leave (entries 8 and 10).
    - `tests/control/test_settings.c`: the name counts, and default, flag and topic rows for the two settings, under the Interface's leave.
    - `tests/control/test_telemetry.c`: the last config entry is now `config/command_topic`, under the Interface's leave for config entries.
15. factory: started doing as heishamon.
16. builder: Round 2. I fixed the reviewer's two findings from entry 14 and nothing else. No code changed this round.

    **Changed**
    - Finding 1: `docs/control/firmware.md`, *Commands to another HeishaMon*, "How a command goes out", now says `control: published <topic> <value>` is logged only when a publish succeeds. It also names the other two lines and when each appears: `control: publish failed <topic> <value>` when the MQTT client refuses the publish, and `control: command topic too long for <name>, not published` when the topic does not fit its buffer. This matches `publish_command` in `HeishaMon/src/control/glue/ctl_firmware.cpp`.
    - Finding 2: I rewrote the work item's `## Description` to include the human's answers from entries 8–10 (R5). Front matter and journal are untouched.
      - Description, *Smart Grid mode in the router*: value `1` is upstream's "Capacity 1". It stops room heating only because the owner set Capacity 1 on the pump to a curve that never heats. Another pump needs the same installer setting first. Value `2` is never sent because it stops hot water too.
      - Description, *The counters*: `inputs/frames_dropped` counts only `DROPPED`. A `SENT` frame, and a whole received frame that is not main data, count as neither decoded nor dropped.
      - Interface, the leave for `test_framelog.c`: adds the two granted rows, "a sent frame" (ends in `D`) and "Received 256 and 257" (`NDDN`), and why each changes.
      - Interface, the leave for `test_router.c`: adds the `config.smart_grid = false;` line in `config_with`.
      - Criterion 3: names "Received 256 and 257" as the one F0011 row that changes.
      - Criterion 9 asks for three more things, all already in the docs:
        - the Capacity 1 explanation;
        - the three publish log lines;
        - that `frames_dropped` counts only frames that broke.
        The decision entry it asks for now includes the Capacity 1 explanation and `2` rejected for no demand. Criteria 8 (`make check`) and 9 (`docs:`) are still last.

    **Checks**
    - `make check` is green: lint, test, device, firmware, then `check: green`.
    - Smoke test: `make firmware | grep -c -E '^(esp8266|esp32):'` printed `2`, as expected.

    **Noticed, not touched**
    - `docs/control/router.md` still describes only the thermostat path. The docs criterion does not name it.
    - The ESP32 build sometimes crashes with an internal compiler error, as reported in entries 3 and 12. It did not happen this round.

    The factory left out changes outside the write paths: factory/features/F0013-commands-over-mqtt/in-progress/F0013-S0001-smart-grid-remote.md.

    `check`: check: green
17. factory: started review as heishamon.
18. reviewer: Round 2 review. The code, tests and docs have no findings. One item stays open, and nobody in the pipeline as configured can close it, so I'm asking you rather than sending the work item back.

    **Question: who folds your answers (entries 8–10) into the work item's `## Description`?**

    Finding 2 of entry 14 still holds. The Description does not match what was built (R5):
    - The Interface's leave for `tests/control/test_framelog.c` still allows only "the sent-frame rows now expect `SENT`" and says "Nothing else in those files changes". It does not name the rows you allowed: "a sent frame" (now `NSNDD`) and "Received 256 and 257" (now `NDDN`).
    - The leave for `tests/control/test_router.c` does not name the line `config.smart_grid = false;` in `config_with`.
    - Criterion 3 still says "Every received-frame row of F0011 still holds", but the row "Received 256 and 257" changed.
    - Criterion 9 does not ask for:
      - the Capacity 1 explanation (`2` is never sent, and another pump needs the same installer setting);
      - the three publish log lines;
      - the rule that `frames_dropped` counts only frames that broke.
    - The *counters* paragraph does not say that a whole received frame that is not main data counts as neither decoded nor dropped.

    The builder rewrote the Description in round 2 (entry 16), but the factory dropped that change. The builder's `write_paths` in `factory/pipeline.yml` do not include `{work_item}`; only the reviewer's do. Sending the work item back to `doing` would repeat this until the round cap.

    Options:
    - (a) You fold the answers into the Description in your own commit. Entry 16 lists the changes.
    - (b) You tell the reviewer to fold them in on the next review run, as entry 16 lists them, with the two closing criteria kept last. Then the reviewer approves if nothing else is found.
    - (c) You declare that entries 8–10, which you marked "part of the Description", are enough as they stand. The reviewer then approves without rewriting.

    I would choose (b). The text is already drafted, and the reviewer is the only role that can write the work item. You may also want to give the builder `{work_item}` in `factory/pipeline.yml` for future stories; that file is yours.

    **Checked and fine in this round**
    - **Entry 14, finding 1 is fixed.** `docs/control/firmware.md`, "How a command goes out", now says `control: published <topic> <value>` is logged only when the publish succeeds. It names `control: publish failed <topic> <value>` and `control: command topic too long for <name>, not published`. Both match `publish_command` in `HeishaMon/src/control/glue/ctl_firmware.cpp`.
    - **Criteria 1 and 4:** `tests/control/test_smart_grid.c` covers them:
      - rows with `heat_cool_heat_value` -1 and 1;
      - exactly one `SetSmartGridMode` per send: `0` for heating or cooling demand, `1` for no demand;
      - landing on the reported value;
      - a repeat every retry interval up to `max_retries`, then `FAULT`;
      - the five dispatch values.
    - **Criterion 2:** `make check` is green. The `test_router.c` leave was granted in entries 8 and 10.
    - **Criterion 3:** `tests/control/test_framelog_sent.c` covers:
      - the sent frame (`S`) and then the answer (`F`);
      - checksum 144 giving `D`;
      - the 111-byte query over four lines (`NNNS`);
      - `ctl_framelog_is_optional_pcb` true for the 20-byte frame and false otherwise;
      - each header limit on both sides: count 1 and 2, count 256 and 257, checksum 255 and 256.
    - **Criterion 5:** `tests/control/test_gate.c` keeps every existing row and adds the remote rows, including open while listen-only and without an optional PCB.
    - **Criterion 6:** `tests/control/test_settings.c`: `smart_grid` accepts 0 and 1 and refuses 2; `command_topic` accepts `jeisha/commands` and the empty text and refuses `a+b`; the name count is 23.
    - **Criterion 7:** no upstream file and no hook line is in the diff.
    - **Criterion 8:** the factory's `make check` after the builder was green (entry 16).
    - **Criterion 9:** the docs and the decision entry say all the points it asks for.
    - **Architecture and embedded safety:**
      - the `.c` files are pure, with no dynamic memory and no recursion;
      - a sent frame's index stays within bounds (`expected` is at most 255 before the checksum byte is appended);
      - text buffers are written with their size;
      - the result of `publish` is checked.
    - **Answer 8:** you accepted that the rules only in the firmware glue have no host test.

    **Earlier tests changed (status M)**
    - `tests/control/test_framelog.c`: the `SENT` letter and the sample fall under the Interface's leave; rows 295 and 303 under your leave in entries 8 and 10.
    - `tests/control/test_gate.c`: the new arguments and the remote rows fall under the Interface's leave.
    - `tests/control/test_router.c`: the `config_with` line falls under your leave in entries 8 and 10.
    - `tests/control/test_settings.c`: the name counts and the default, flag and topic rows fall under the Interface's leave.
    - `tests/control/test_telemetry.c`: the last config entry, now `config/command_topic`, falls under the Interface's leave.
