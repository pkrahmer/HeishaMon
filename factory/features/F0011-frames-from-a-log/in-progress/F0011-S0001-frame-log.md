---
stage: review
blocked: null
round: 0
attempts: 0
crashes: 0
pull_request: 25
comments_read: []
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
