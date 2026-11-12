---
stage: review
blocked: null
round: 1
attempts: 0
crashes: 0
pull_request: 25
comments_read: [6839264563]
handed_over: null
claim: null
journal:
- number: 1
  speaker: human
  kind: created
  at: '2026-11-12T23:16:29.900288Z'
  stage: doing
- number: 2
  speaker: factory
  kind: started
  at: '2026-11-12T23:16:29.906392Z'
  stage: doing
- number: 3
  speaker: builder
  kind: result
  at: '2026-11-12T23:30:48.925475Z'
  stage: doing
  run: fea7d25afbb84620b487227a8d3b41c3
- number: 4
  speaker: factory
  kind: started
  at: '2026-11-12T23:30:57.783863Z'
  stage: review
- number: 5
  speaker: reviewer
  kind: result
  at: '2026-11-12T23:32:44.111475Z'
  stage: review
  run: 4f960e63056f4aae8c64f404d20de3f1
- number: 6
  speaker: factory
  kind: started
  at: '2026-11-12T23:32:50.202907Z'
  stage: doing
- number: 7
  speaker: builder
  kind: result
  at: '2026-11-12T23:41:14.237220Z'
  stage: doing
  run: 84f90f56c2554a95852b7d220f5d2450
- number: 8
  speaker: factory
  kind: started
  at: '2026-11-12T23:41:20.524741Z'
  stage: review
- number: 9
  speaker: reviewer
  kind: question
  at: '2026-11-12T23:42:29.700477Z'
  stage: review
  run: 1414d235b7764e4face0732fff41a3d9
  blocked: question
- number: 10
  speaker: human
  kind: comment
  at: '2026-11-12T23:44:16.450990Z'
  stage: review
---

# The frames from a log topic

## Description

HeishaMon with hex dumps on writes every frame it receives from the pump into its log. With "log to MQTT" on, the same lines go to `<base>/log`, one message per line. A received main data frame appears as three kinds of line, in this order, each prefixed with a time stamp and the board's milliseconds:

- `Wed Nov 11 22:25:49 2026 (594966970): Received 203 bytes data`;
- seven lines `...: data: 71 C8 01 10 ...`, each with up to 32 bytes as two uppercase hex digits and a space;
- `...: Checksum and header received ok!`.

The frames the board sends (queries, optional PCB frames) are logged the same way, but after a line `sent bytes: <n> including checksum value: <v>`, never after `Received <n> bytes data`.

This story adds a setting `frame_log_topic`. When it is set, the glue subscribes to that topic and feeds each message to a pure C parser. The parser rebuilds a received frame from its lines. When a main data frame is complete and its checksum holds, the glue hands it to upstream's `decode_heatpump_data` with the board's own globals, then to `ctl_firmware_frame`. The board then publishes the decoded values under its own base as if it were wired to the pump, and its loop takes the pump values from them.

## Interface

New files: `HeishaMon/src/control/glue/ctl_framelog.h`, `ctl_framelog.c`, `tests/control/test_framelog.c`. Changed: `ctl_settings.h`, `ctl_settings.c`, `ctl_dispatch.h`, `ctl_dispatch.c`, `ctl_firmware.cpp`, and the tests `tests/control/test_settings.c` and `tests/control/test_dispatch.c` by the owner's leave as the criteria say.

```c
/* ctl_framelog.h */
#define CTL_FRAMELOG_MAX 256

typedef struct {
    bool collecting;
    size_t expected;       /* the <n> of "Received <n> bytes data" */
    size_t length;
    uint8_t frame[CTL_FRAMELOG_MAX];
} ctl_framelog;

typedef enum { CTL_FRAMELOG_NOTHING, CTL_FRAMELOG_FRAME, CTL_FRAMELOG_DROPPED } ctl_framelog_result;

void ctl_framelog_init(ctl_framelog *log);
ctl_framelog_result ctl_framelog_line(ctl_framelog *log, const char *line, size_t length);
bool ctl_framelog_checksum_ok(const uint8_t *frame, size_t length);
```

- **The message text.** The parser reads the text after the first `): `. A line without it gives `NOTHING`.
- **`Received <n> bytes data`** with `n` from 1 to `CTL_FRAMELOG_MAX` starts a frame of `n` bytes. A frame in progress is then `DROPPED`, and the new one starts.
- **`data: ` followed by hex bytes**, while collecting, appends the bytes. A byte is two hex digits, upper or lower case, followed by a space or the end of the line. More bytes than expected, or a malformed byte, ends the frame as `DROPPED`. While not collecting, the line gives `NOTHING`.
- **When the frame is complete**, the line that completes it gives `FRAME` if `ctl_framelog_checksum_ok` holds, and `DROPPED` otherwise; collecting ends either way.
- **`sent bytes: ...`** ends a frame in progress as `DROPPED`, and starts nothing.
- Every other line gives `NOTHING` and changes nothing.
- `ctl_framelog_checksum_ok` is true when the sum of all bytes, the checksum included, is 0 modulo 256, as `isValidReceiveChecksum` checks.

The setting and the dispatch:

- **The setting.** `frame_log_topic` follows `defrost_topic` in the name order, with the topic rule and an empty default. The name count with `rooms` 0 becomes 21.
- **The dispatch.** `ctl_dispatch_topic` gives a new kind, `CTL_TOPIC_FRAME_LOG`, for a topic equal to a non-empty `frame_log_topic`. It checks this after the rooms, the PV and the pump topics.

The glue:

- **Subscribing.** It subscribes to a non-empty `frame_log_topic`.
- **Feeding.** It feeds each message of that topic to the parser, in arrival order, with a static `ctl_framelog`.
- **A complete frame.** On `FRAME` with 203 bytes, `frame[0]` `0x71` and `frame[3]` `0x10`, it calls `decode_heatpump_data` with the frame, HeishaMon's `actData`, `mqtt_client`, `log_message`, `heishamonSettings.mqtt_topic_base` and `heishamonSettings.updateAllTime`, through `extern` declarations of those globals. Then it calls `ctl_firmware_frame` with the frame. Any other frame is counted and ignored.
- **Counters.** It counts the decoded frames and the dropped ones, and publishes both as telemetry entries `inputs/frames_decoded` and `inputs/frames_dropped`, appended to the inputs group. The rows of `tests/control/test_telemetry.c` that state the inputs count change by the owner's leave.
- **No upstream change.** No upstream file changes in this story.

## Acceptance criteria

1. The parser on a sequence in the logged form: the builder writes `tests/control/data/framelog_sample.txt` with the lines of one received 203-byte main data frame (seven `data:` lines of up to 32 bytes, first bytes `71 C8 01 10`, a valid checksum as last byte), followed by a sent 20-byte frame and a received 20-byte answer, each line with a time stamp and milliseconds as in the Description. The received main frame gives `NOTHING` for its header and first six data lines and `FRAME` on the seventh, with the frame equal to the bytes written.
2. The parser's refusals, as rows of one table test:
   - the same frame with one byte changed gives `DROPPED`, because of the checksum;
   - a frame with a data line missing, followed by the next `Received` line, gives `DROPPED` at that line;
   - a sent frame (a `sent bytes:` line and its data lines) gives only `NOTHING` and `DROPPED`, never `FRAME`;
   - a malformed byte `7G` gives `DROPPED`;
   - `Received 0 bytes data` and `Received 300 bytes data` start nothing;
   - a line without `): ` gives `NOTHING`.
3. `ctl_framelog_checksum_ok`: the 20-byte answer `71 11 01 50 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 2D` is true, and the same with its last byte changed is false.
4. The setting and the dispatch:
   - `frame_log_topic` accepts `jeisha/log` and the empty text, and refuses `a+b`;
   - the name count is 21;
   - `ctl_dispatch_topic` gives `CTL_TOPIC_FRAME_LOG` for `jeisha/log` when it is set, and `NONE` when it is empty.
5. `make firmware` compiles both boards, and `git diff develop --stat -- HeishaMon ':!HeishaMon/src'` shows no upstream file changed.
6. `make check` stays green.
7. docs: `docs/control/firmware.md` lists the setting, and gets a section *Frames from another HeishaMon's log*. It says:
   - what the production board must have on (hex dumps, log to MQTT);
   - that the four pump topics must be empty;
   - what the dev board then publishes;
   - the two new counters;
   - the risk on the production side: HeishaMon drops its MQTT connection when a log publish fails.

## Smoke test

```sh
make firmware | grep -c -E '^(esp8266|esp32):'
```

Expect: `2`

## Journal

1. 2026-11-12 human: created.
2. factory: started doing as heishamon.
3. builder: Built the frame log path. New: HeishaMon/src/control/glue/ctl_framelog.h, ctl_framelog.c (pure parser, reads lines without a terminating zero by their length, never past it), tests/control/test_framelog.c (the sample, a 14-row refusal table, the checksum), tests/control/data/framelog_sample.txt (a 203-byte main frame 71 C8 01 10 with a valid checksum, then a sent 20-byte frame and the 20-byte answer, all with time stamp and milliseconds). Changed: ctl_settings.h/.c (frame_log_topic after defrost_topic, topic rule, empty default, CTL_SETTINGS_FIXED_NAMES 21), ctl_dispatch.h/.c (CTL_TOPIC_FRAME_LOG after rooms, PV and pump topics, before the setting topics), ctl_telemetry.h/.c (frames_decoded and frames_dropped in ctl_telemetry_source, appended to the inputs group after the rooms; the Interface did not list these two files but the counters need them), ctl_firmware.cpp/.h (subscribes to frame_log_topic with the other input topics; feeds each message to a static ctl_framelog; on FRAME of 203 bytes with 0x71 and 0x10 in byte 3 calls decode_heatpump_data with actData, mqtt_client, log_message, heishamonSettings.mqtt_topic_base and updateAllTime through extern declarations, then ctl_firmware_frame; fills the counters into the telemetry source). The file's static pointer mqtt_client was renamed to control_client so that it no longer clashes with upstream's global of the same name. ctl_firmware.cpp now includes upstream's webfunctions.h for settingsStruct.

   Tests: test_settings.c gets test_frame_log_topic (jeisha/log and empty accepted, a+b refused, not a pump topic) and the name counts shifted by the new setting (20->21, 24->25, 22->23, last index 23->24); test_dispatch.c gets test_frame_log_topic (FRAME_LOG when set, NONE when empty, room/PV/defrost win on a shared topic). test_telemetry.c: count rows 21->23, refused index 21->23, 9+3*rooms -> 11+3*rooms; beyond pure count rows I also added the two counter rows to the inputs table and set the counters in set_fixture, without which the fixture's new fields are uninitialised and the inputs table cannot match the new count. I wrote the parser before running the tests, so I never saw them fail first; to check that they bite, I made a temporary copy of the parser with the checksum always true, and the 'one byte changed' row and the checksum test then failed.

   Decisions (recorded in docs/control/decisions.md, 'Frames from another HeishaMon's log'): a sent mark is any text containing 'sent bytes: ' (the ESP32 build logs 'heatpump request query sent bytes: <n>'), rejected matching only at the start; 'Received 0/300 bytes data' starts nothing but still drops a frame in progress; a data line with no bytes adds nothing; whole frames that are no main data frame (optional PCB answer, extra block) count as dropped (rejected a third counter); the parser is reset when the topics move; frame_log_topic is not a pump topic for ctl_settings_pump_from_mqtt. Docs: docs/control/firmware.md lists the setting and the two counters in its tables, names frame_log_topic among the topics whose change moves subscriptions, and has the section 'Frames from another HeishaMon's log' (production board needs Debug hexdump and Debug log to MQTT, the four pump topics must be empty, what the spare board publishes, the counters, the disconnect risk on a failed log publish).

   Smoke test: make firmware | grep -c -E '^(esp8266|esp32):' gives 2. git diff develop --stat -- HeishaMon ':!HeishaMon/src' is empty. make check: green.

   Noticed, not touched: with log to MQTT on, the spare board logs its own decoding; setting its frame_log_topic to its own log topic would feed it back, and there is no guard. With the optional PCB emulation on at the production board, frames_dropped grows steadily from the PCB answers, as documented.

   `check`: check: green
4. factory: started review as heishamon.
5. reviewer: Findings:

   1. tests/control/test_framelog.c test_refusals: the Interface says `Received <n> bytes data` starts a frame for n from 1 to CTL_FRAMELOG_MAX. 0/256/257 (and 300) are tested, but nothing tests that n = 1 starts a frame, so the lower bound has only one side covered. Fix: add a row such as `Received 1 bytes data` + `data: 00` that expects `NF` (a 1-byte zero frame passes the checksum), or `Received 1` + `sent bytes:` that expects `ND`.
   2. Against the story, HeishaMon/src/control/glue/ctl_firmware.cpp is_main_data_frame/handle_frame_log: the glue rule "FRAME with 203 bytes, frame[0] 0x71 and frame[3] 0x10 is decoded, any other frame is counted and ignored", and the two counters that follow from it, have no criterion and no test for the failing side. A whole 20-byte answer, or a 203-byte frame with frame[3] != 0x10, going to decode_heatpump_data would pass every check. Fix: the human decides whether to add a criterion (for example a pure classifier in ctl_framelog.c with a table test) or to accept the gap knowingly.
   3. Work item `## Description`, Interface "New files / Changed": the list leaves out ctl_telemetry.h, ctl_telemetry.c and ctl_firmware.h, which the build changed. It also limits the test_telemetry.c leave to "the rows that state the inputs count", while the build also added the two counter rows to the inputs table and the counter values to set_fixture. Fix: rewrite the Interface's file list and the test_telemetry.c sentence so they match what was built.
   4. HeishaMon/src/control/glue/ctl_firmware.h:38 (111 chars), :45 (170 chars), ctl_settings.h:61 (115), ctl_telemetry.h:35 (123): the edited comments run past 100 columns, and no line on develop does. Fix: re-wrap those comments at 100 columns, as the rest of each header is.

   Checked with no finding:
   - The parser's rules and refusals (criteria 1–3): the sample plays NNNNNNNFNNNNFN, and both frames are compared byte for byte.
   - Settings and dispatch order (criterion 4).
   - Architecture: the .c files are pure, no upstream file changed, and upstream headers are included only from the .cpp.
   - Embedded safety: every loop is bounded by length or expected, no frame write can exceed CTL_FRAMELOG_MAX, and there is no dynamic memory.
   - Against upstream: the decode_heatpump_data signature, the actData/mqtt_client/heishamonSettings/log_message globals, the format of logHex and the "Received %d bytes data" line, the "sent bytes:" lines, and log_message's disconnect when a publish fails all match what firmware.md and decisions.md claim.
   - The builder's make check was green, per the journal.

   Earlier tests (status M):
   - tests/control/test_dispatch.c: only adds test_frame_log_topic, and every earlier test is unchanged.
   - tests/control/test_settings.c: name-count rows 20→21, 24→25, 22→23 and index 23→24/24→25 changed under the story's leave ("test_settings.c … by the owner's leave"); the defaults table row and test_frame_log_topic are additions.
   - tests/control/test_telemetry.c: the count rows (21→23, refused index 23, 11+3*rooms) changed under the story's leave. The two inputs-table rows and the fixture counters are part of that leave, because CHECK_GROUP compares the inputs table one row per counted entry, so the table states the count. Finding 3 asks for the Description to say so.
6. factory: started doing as heishamon.
7. builder: Round 1: fixed the reviewer's four findings and nothing else.

   1. Lower bound of `Received <n>`: tests/control/test_framelog.c test_refusals has a new row, "Received 1" (received_one). `Received 1 bytes data` followed by `data: 00 ` gives F, and `Received 1 bytes data` followed by a `sent bytes:` line gives D. The row expects `NFND`.
   2. The main-data-frame rule had no test on its failing side. I moved it out of ctl_firmware.cpp into the pure function `ctl_framelog_is_main_data(const uint8_t *frame, size_t length)` in HeishaMon/src/control/glue/ctl_framelog.h/.c. It uses a new macro, `CTL_FRAMELOG_MAIN_LENGTH 203`, and checks header 0x71 and type 0x10 in byte 3. handle_frame_log in ctl_firmware.cpp now calls it, and the cpp's static is_main_data_frame and MAIN_FRAME_* constants are gone. The new table test test_main_data covers the sample frame (true), one byte short, another header and another type (all false). It also checks that the 20-byte answer and NULL are false. I wrote it first and it failed to compile (implicit declaration) before the function existed. The counting in the .cpp is still covered only by make firmware. The rule's step from the .cpp to pure code is recorded in docs/control/decisions.md under 'Frames from another HeishaMon's log' (rejected: leaving the rule untested in the .cpp). docs/control/firmware.md names the function where it describes the main data frame. I did not add an acceptance criterion: the Interface now states the function and the tests cover it. The human may still want a numbered criterion for it.
   3. Description: in the Interface I rewrote the file list. It now names framelog_sample.txt, ctl_telemetry.h/.c, ctl_firmware.h and test_telemetry.c. I added `CTL_FRAMELOG_MAIN_LENGTH` and `ctl_framelog_is_main_data` to the header sketch, with a bullet on what the function does. The 'A complete frame' bullet now refers to the function. The test_telemetry.c sentence now names all three changes: the count rows, the two counter rows in the inputs table, and the counters in the fixture.
   4. Re-wrapped at 100 columns: the comments in ctl_firmware.h (subscribe and mqtt), ctl_settings.h (defaults) and ctl_telemetry.h (count), plus the new condition line in ctl_firmware.cpp. No added line in a header or a .c/.cpp file now runs past 100 columns. Some added Markdown lines in firmware.md and the sample's data lines do.

   Smoke test: `make firmware | grep -c -E '^(esp8266|esp32):'` gives 2. `git diff develop --stat -- HeishaMon ':!HeishaMon/src'` is empty. make check: green.

   Noticed, not touched: the same as in the earlier entry. A spare board that has its frame_log_topic set to its own log topic would feed itself, and nothing guards against it. With PCB emulation on at the production board, frames_dropped grows steadily.

   The factory left out changes outside the write paths: factory/features/F0011-frames-from-a-log/in-progress/F0011-S0001-frame-log.md.

   `check`: check: green
8. factory: started review as heishamon.
9. reviewer: Round 1 review. The code fixes for the four findings of entry 5 are in: received_one row "NFND", ctl_framelog_is_main_data with test_main_data, headers re-wrapped. But finding 3 of entry 5 cannot be closed by the builder, so I am asking instead of sending the item back.

   Question: the `## Description` in the committed work item is still the original. The builder rewrote it, but the builder's write paths do not include the work item, so the factory dropped the change (entry 7, last line). Sending the item back to doing would only repeat that. Three points in the Description do not match what was built:
   - (a) The Interface's file list leaves out ctl_telemetry.h/.c, ctl_firmware.h and tests/control/data/framelog_sample.txt.
   - (b) The header sketch and the 'A complete frame' bullet do not name CTL_FRAMELOG_MAIN_LENGTH or ctl_framelog_is_main_data.
   - (c) The leave for test_telemetry.c covers only "the rows that state the inputs count". The diff also adds the rows inputs/frames_decoded "42" and inputs/frames_dropped "3" to the inputs table in test_entries, and sets the two counters in set_fixture.

   Options:
   1. You rewrite the Description yourself, and give your word on the wider test_telemetry.c leave in (c).
   2. You tell me to make exactly that rewrite in the work item (my write paths include it), with your leave for (c) stated.
   3. You accept the Description as it is, knowingly.

   I would choose 2. (c) extends a test leave, which only you can grant, so I have not edited the Description on my own.

   Findings, for the builder's next round whatever the answer:
   1. docs/control/decisions.md:806 (151 chars) and :816 (166 chars): text added in round 1 was appended without re-wrapping, and no line of decisions.md on develop runs past 100 columns. Fix: re-wrap the entry 'Frames from another HeishaMon's log' at 100 columns.
   2. docs/control/firmware.md:406 (143), :416 (103), :421 (126): new prose lines in the section 'Frames from another HeishaMon's log' run past 100 columns. Develop's only long prose lines are :229 and :378. Fix: re-wrap the three lines at 100 columns.

   Checked with no finding:
   - Criteria 1–4 each have a test that bites. The limits are tested on both sides: Received 0/1 and 256/257/300; bytes beyond the count; a malformed byte and a missing space; a line without the stamp; checksum true and false; main-data true and the short, other-header, other-type and NULL cases.
   - Dispatch order is tested against rooms, PV and defrost.
   - Criterion 5: `git diff develop --stat -- HeishaMon ':!HeishaMon/src'` is empty.
   - make check was green after the builder (entry 7).
   - Architecture: ctl_framelog.c is pure C99 and its helpers are static. Upstream headers and globals are used only from ctl_firmware.cpp, and their extern declarations match HeishaMon.ino and decode.h.
   - Embedded safety: frame writes are bounded by expected <= CTL_FRAMELOG_MAX, and the count parse is capped at 4 digits. decode_heatpump_data copies DATASIZE (203) bytes from the 256-byte frame. The payload is fully consumed before any publish reuses the client's buffer.
   - The firmware.md and decisions.md claims match the code: the counters, the reset on a topic move, the sent mark anywhere in the line, and pump_from_mqtt not counting frame_log_topic.

   Earlier tests (status M):
   - tests/control/test_settings.c: the name-count/index rows changed under the story's leave; test_frame_log_topic and the defaults row are additions.
   - tests/control/test_dispatch.c: adds test_frame_log_topic only, and the earlier tests are unchanged.
   - tests/control/test_telemetry.c: the count rows changed under the story's leave. The two inputs-table rows and the fixture counters are the subject of question (c).
10. 2026-11-12 human: 2. Reviewer, rewrite the Description exactly for (a) and (b). For (c), the leave widens: in `tests/control/test_telemetry.c` the inputs table may gain the two counter rows and the fixture may set the two counters, and nothing else in that file changes. Builder: the two wrapping findings.

    — Kumpel, for the human who owns this project
    <!-- kumpel -->
