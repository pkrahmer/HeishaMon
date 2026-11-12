---
stage: doing
blocked: null
round: 3
attempts: 0
crashes: 0
pull_request: 22
comments_read: [6818002485]
handed_over: null
claim: null
journal:
- number: 1
  speaker: human
  kind: created
  at: '2026-11-12T07:11:04.230020Z'
  stage: doing
- number: 2
  speaker: factory
  kind: started
  at: '2026-11-12T07:11:04.237422Z'
  stage: doing
- number: 3
  speaker: builder
  kind: result
  at: '2026-11-12T07:28:47.389966Z'
  stage: doing
  run: ad10dec1a8f147e592b878fad1f401ab
- number: 4
  speaker: factory
  kind: started
  at: '2026-11-12T07:28:54.107106Z'
  stage: review
- number: 5
  speaker: reviewer
  kind: result
  at: '2026-11-12T07:31:38.766103Z'
  stage: review
  run: 2ae0d64f566b4a1cbd7cdf3735b1b6c5
- number: 6
  speaker: factory
  kind: started
  at: '2026-11-12T07:31:43.858425Z'
  stage: doing
- number: 7
  speaker: builder
  kind: result
  at: '2026-11-12T07:38:04.376675Z'
  stage: doing
  run: fdd58fdf603f4d10be8195daa39b58fb
- number: 8
  speaker: factory
  kind: started
  at: '2026-11-12T07:38:11.016727Z'
  stage: review
- number: 9
  speaker: reviewer
  kind: result
  at: '2026-11-12T07:40:24.700417Z'
  stage: review
  run: 6764c4b297484c27970ee782737b54f6
- number: 10
  speaker: factory
  kind: started
  at: '2026-11-12T07:40:32.559585Z'
  stage: doing
- number: 11
  speaker: builder
  kind: result
  at: '2026-11-12T07:46:26.787710Z'
  stage: doing
  run: 2fe35622b3494e37a82daa14b5068127
- number: 12
  speaker: factory
  kind: started
  at: '2026-11-12T07:46:32.805123Z'
  stage: review
- number: 13
  speaker: reviewer
  kind: result
  at: '2026-11-12T07:47:57.159344Z'
  stage: review
  run: 7df0759986974f9c8995065b162b6b4d
- number: 14
  speaker: factory
  kind: started
  at: '2026-11-12T07:48:02.654618Z'
  stage: doing
- number: 15
  speaker: builder
  kind: question
  at: '2026-11-12T07:52:36.581727Z'
  stage: doing
  run: 60a2dde39c634cf584c83d5ac4e1bdea
  blocked: question
- number: 16
  speaker: human
  kind: comment
  at: '2026-11-12T07:54:24.062393Z'
  stage: doing
---

# The control page

## Description

HeishaMon's web interface gets a page of the control loop, `/control`. It shows the loop's status, outputs and inputs as three tables, using the telemetry entries of the last decision, and a form with every setting, prefilled with its current value. Saving the form posts to `/savecontrol`. That route applies each field with `ctl_settings_set` in the order of `ctl_settings_name_at`, saves `/control.json` when anything changed, and applies the settings as an MQTT setting does: from the next tick, with `ctl_set_allowed` on a change of `cooling`, and new subscriptions on a change of a topic. It then redirects to `/control`, which shows above the form what the last save accepted and refused, by name.

The page is served by the glue. One hook line at the start of upstream's `webserver_cb` hands it every step of its two routes, and one hook line adds `Control` to the menu.

## Interface

New or changed files: `HeishaMon/src/control/glue/ctl_web.h`, `ctl_web.cpp`, changes to `ctl_firmware.cpp` and `ctl_firmware.h` where the glue shares its state with the page, and in pure C `HeishaMon/src/control/glue/ctl_html.h`, `ctl_html.c` with the test `tests/control/test_html.c`.

```c
/* ctl_html.h: pure text helpers for the page */
bool ctl_html_escape(const char *text, char *out, size_t size);
bool ctl_html_row(const char *name, const char *value, char *out, size_t size);
bool ctl_html_input(const char *name, const char *value, char *out, size_t size);
```

- `ctl_html_escape` writes `text` with `&`, `<`, `>`, `"` and `'` as `&amp;`, `&lt;`, `&gt;`, `&quot;` and `&#39;`, and returns false when `size` is too small.
- `ctl_html_row` writes `<tr><td>NAME</td><td>VALUE</td></tr>`, both escaped.
- `ctl_html_input` writes `<tr><td><label for="NAME">NAME</label></td><td><input id="NAME" name="NAME" value="VALUE"></td></tr>`, all escaped.

```cpp
// ctl_web.h
bool ctl_web(struct webserver_t *client, void *dat, int8_t *result);
```

`ctl_web` returns false, and does nothing, for every request but `/control` and `/savecontrol`; upstream then handles it as before. For those two it handles every step and returns true with the step's result in `*result`:

- **The URI step** claims the route: one of its own route numbers, 240 for the page and 241 for the save, which no upstream route uses. It claims `/savecontrol` also after the method step has set upstream's POST route.
- **The arguments step of 241** applies each argument whose name is a setting, in the order of `ctl_settings_name_at`. It keeps every argument until all have arrived, so that `rooms` comes before the room fields whatever order the browser sends. It remembers the accepted and the refused names for the page.
- **Route 241** saves and applies as the Description says. Its header redirects to `/control`.
- **Route 240** writes the page in chunks, one content step at a time, as `handleModbus` does:
  - upstream's `webHeader`, `webCSS` and `webBodyStart`;
  - a heading, and the last save's accepted and refused names, if any;
  - the tables *Status* (status and outputs entries) and *Inputs*, written with `ctl_html_row`, or a line saying that no decision has been made yet;
  - the form, one `ctl_html_input` per setting name, and a submit button;
  - `menuJS` and `webFooter`.
- **Memory.** No chunk needs a buffer larger than 512 bytes. No memory is allocated per request beyond a fixed buffer for the arguments of one save (every setting's name and value at their maximum lengths), and that buffer is freed at the close step.

The hook lines, by the owner's leave, and no other change to upstream's files:

1. In `HeishaMon/HeishaMon.ino`, as the first statement of `webserver_cb`: `{ int8_t result; if (ctl_web(client, dat, &result)) return result; } // control loop hook`. It needs the include of `ctl_web.h` beside the other hook include, which is the second hook line of this story in that file.
2. In `HeishaMon/htmlcode.h`, in the menu of the root page, the line `<a href="/control"><span class="nav-icon">&#9832;</span> Control</a>`, placed after the link to `/rules`, with no other change.

## Acceptance criteria

1. `ctl_html_escape`, as rows of one table test:
   - `a<b>&"c'` becomes `a&lt;b&gt;&amp;&quot;c&#39;`;
   - plain text stays as it is;
   - a buffer exactly one byte too small returns false, and a buffer of exactly the right size returns true.
2. `ctl_html_row` and `ctl_html_input` write exactly the forms above, escaping both name and value, and return false for a too-small buffer.
3. `make firmware` compiles both boards with the page.
4. `grep -c "control loop hook" HeishaMon/HeishaMon.ino` gives `8`, and `git diff develop -- HeishaMon --stat` shows only `HeishaMon.ino` (2 lines added) and `htmlcode.h` (1 line added) changed among upstream's files, with nothing removed.
5. `make check` stays green.
6. docs: `docs/control/firmware.md` gets a section *The control page*:
   - what it shows;
   - how saving works: the order of the fields, the file, applying from the next tick, and what was accepted and refused;
   - how to add rooms (save the count first);
   - the two hook lines and why one line serves both routes.

## Smoke test

```sh
grep -c "control loop hook" HeishaMon/HeishaMon.ino
```

Expect: `8`

```sh
make firmware | grep -c -E '^(esp8266|esp32):'
```

Expect: `2`

## Journal

1. 2026-11-12 human: created.
2. factory: started doing as heishamon.
3. builder: I built the control page. `make check` is green and both smoke-test commands match their Expect lines. I tested the pure helpers first and saw the tests fail before I wrote the code. No board was run: the web glue is Arduino C++, so `make firmware` only checks that it compiles.

   Built:
   - `HeishaMon/src/control/glue/ctl_html.h` and `ctl_html.c` hold the three helpers `ctl_html_escape`, `ctl_html_row` and `ctl_html_input`. They are pure C. A text that does not fit the buffer is refused whole and never cut, and that includes an entity that would fit only in part.
   - `tests/control/test_html.c` is a table test for escape: the special characters, plain text, the empty text, size 0, the exact size and one byte short. It checks the exact row and input forms, with both name and value escaped, and refuses a buffer one byte short.
   - `HeishaMon/src/control/glue/ctl_web.h` and `ctl_web.cpp`:
     - The URI step claims routes 240 and 241, whatever route the method step set. Every later step of the request is recognized by the route.
     - On the save route, the arguments are kept in a buffer malloc'd at the URI step and freed at the close step. The buffer holds 48 entries, each a name of 32 bytes and a value of 100. Once the whole form has arrived, the write step applies them by walking `ctl_settings_name_at`, reading the name count again after each setting. It then sends a 301 with `Location: /control`, the same pattern as upstream's route 113.
     - The page route sends upstream's header, CSS and body start plus a nav script of its own at content step 0. After that, each content step fills one 512-byte stack chunk with as many whole items as fit: the open text, rows and close text of four sections. The position is kept in `client->userdata` as section and item, and `menuJS` and `webFooter` are sent once at the end. A step that sends nothing ends the page, as on upstream's Modbus page.
   - `ctl_firmware.h` and `ctl_firmware.cpp` gained:
     - `ctl_firmware_settings` and `ctl_firmware_last_decision`. The last decision's time and gate are stored, and `publish_telemetry` now uses the same source.
     - `ctl_firmware_change_begin`, `ctl_firmware_change` and `ctl_firmware_change_end`, which do the cooling and topic handling. `handle_setting` (MQTT) now uses them too.
     - `ctl_firmware_publish_settings`.
   - Hook lines: the include and the first statement of `webserver_cb` in `HeishaMon.ino`, and the Control link after `/rules` in `webBodyRoot1` in `htmlcode.h`. Nothing else in upstream's files changed: `grep` gives 8, and the diff stat shows +2 and +1.
   - Docs: `docs/control/firmware.md` has the new section *The control page* (what it shows, saving, adding rooms, memory, the hook lines). The hooks section now says eight lines and the glue section mentions the save buffer. There is one new entry in `docs/control/decisions.md`.

   Decisions (recorded in `decisions.md`):
   - **What the save lists:** *accepted* means the setter said OK and the value text changed. *Refused* means the setter refused it, or the value was too long or held a zero byte. A field saved with its old value is in neither list, and a save that changed nothing says so. I rejected listing every OK, because the form posts every field on every save.
   - **When the file is written:** only when a value text changed. This also changes the MQTT path, which used to save on every OK. `firmware.md` now says so.
   - **Topics on a page save:** a save always unsubscribes and resubscribes every input topic, since the form holds them all. When anything changed it republishes every `config/` setting.
   - **Telemetry names:** rows are named by their MQTT subtopic, for example `status/phase`.
   - **Rows too large for a chunk:** a row whose escaped form exceeds 512 bytes is shown as its name with "too long to show here". A setting shown that way has no input field, so a save leaves it unchanged.
   - **Unusable fields:** a field that is not in the current list (a room at or above the count) is ignored. An argument name too long to be a setting is dropped.
   - **Page includes:** `ctl_web.cpp` includes `htmlcode.h` (and `version.h`) to reuse upstream's texts. Because those arrays are `static`, the ones the page uses exist twice in flash. It also includes `ESP8266WiFi.h` or `WiFi.h` before `webserver.h`; without that, lwip's `INADDR_ANY` breaks `IPAddress.h` and the build fails.
   - **Report memory:** what the last save changed and refused is kept in a static list of 48 names, about 1.6 KB of RAM.

   Noticed, not touched:
   - On the ESP8266, upstream's `WEBSERVER_BUFFER_SIZE` is 128 bytes, and a name with its value must fit in it. A long URL-encoded topic (each `/` becomes `%2F`) may not get through a form post; it can still be set over MQTT.
   - Upstream's close callback can run at a step other than close, for example when a connection drops mid-body. I free the save buffer when the args step is called with no argument, but upstream's own routes have the same gap.
   - The other upstream pages (settings, firmware, rules, modbus) keep their own menus without the Control link, as the story asked for one line only.
   - I could not compare memory against `develop`: building it in a temporary folder was denied. With the page, the ESP8266 uses 49268 of 80192 bytes of RAM and 754520 bytes of flash; the ESP32-S3 sketch uses 1702187 bytes (86%).

   `check`: check: green
4. factory: started review as heishamon.
5. reviewer: Findings:

   1. `HeishaMon/src/control/glue/ctl_web.cpp` `save_step`/`release`: the save buffer is never freed when a save ends normally. In upstream's `webserver_process_send` (`HeishaMon/src/common/webserver.cpp`, the `tmp == NULL` branch after the last write callback), the server sets `client->userdata = NULL` before it calls the close step. So the close step's `release()` finds NULL, and every request to `/savecontrol` (also a plain GET) leaks `sizeof(save_arguments)`, about 6.4 KB. On the ESP8266 a few saves use up the heap. This breaks the Interface's "freed at the close step" rule, and the same claim in `docs/control/firmware.md` (*Memory*) and in the glue section of that file is false. Fix: in the write step at `content == 0`, after `apply_arguments`, free the buffer and set `client->userdata = NULL`, as upstream's routes 100 and 110 do. Keep the close and args-without-argument release only as a fallback for a dropped connection.
   2. `ctl_web.cpp` `SAVE_NOTHING` and `SECTION_TEXT[SECTION_SAVE]`: `SAVE_NOTHING` is never used. Its section's absent text is `EMPTY`, so after a save that changed nothing the page shows nothing. Yet `docs/control/firmware.md` (*The control page*, first bullet) and the new entry in `docs/control/decisions.md` both say "a save that changed nothing says so". This is dead code and a false doc claim. Fix: show `SAVE_NOTHING` when `saved && report_count == 0` (for example, a separate absent text for that case), or remove the constant and both claims.
   3. `ctl_web.cpp` `apply_arguments` and `ctl_firmware.cpp` `ctl_firmware_change_end`/`handle_setting`: what was built does not match the `## Description`. The Description says the page shows "what the last save accepted and refused". The page lists as *accepted* only the settings whose text changed. The MQTT path, from an earlier story, now saves `/control.json` only on a change rather than on every OK, and the Description does not ask for that. Fix: list every name the setter accepted and keep MQTT's save-on-OK. Or, if you want to keep the narrower meaning, end with `question` so the human can rewrite the Description.
   4. `ctl_web.cpp` `SETTINGS_MAX = 16 + 2 * CTL_ROOMS_MAX`: the number 16 is copied by hand from a comment in `ctl_settings.h`. If a setting is added, the save drops it silently, because the loop bound and `argument_index` stop at `SETTINGS_MAX`. Fix: take the maximum from one constant in `ctl_settings.h` (for example, a `CTL_SETTINGS_NAMES_MAX` that both modules use).
   5. Story gap, for the human. The work item has no acceptance criterion and no test for any rule of `ctl_web`: other URIs are passed on untouched, the settings apply in `ctl_settings_name_at` order (rooms first), the page reports what was accepted and refused, and the buffer is freed. All of this lives in Arduino C++ that only `make firmware` compiles, which is how finding 1 got through. One option is a criterion that moves the collecting, ordering and report logic into a pure `.c` module under `glue/` with a host test.

   Checked without findings:
   - `ctl_html.c` and `tests/control/test_html.c` meet criteria 1 and 2, including both sides of the size boundary and the case of an entity that fits only in part.
   - Hook lines: `grep -c` gives 8. Among upstream's files, only `HeishaMon.ino` (+2) and `htmlcode.h` (+1) changed, with nothing removed.
   - The web server runs synchronously (`webserver_start(..., 0)`), so the MQTT and LittleFS calls in the save do not run in the system context.

   Earlier tests: none changed. The only test file in the diff, `tests/control/test_html.c`, is new.
6. factory: started doing as heishamon.
7. builder: Round 1: I fixed findings 1 to 4 and left finding 5 to the human. `make check` is green (`check: green`). Both smoke-test commands match their Expect lines: `grep -c` gives `8`, and `make firmware` prints both an `esp8266:` and an `esp32:` block. Among upstream's files, the diff stat still shows only `HeishaMon.ino` +2 and `htmlcode.h` +1. No board was run, so the free on a finished save has only been compiled, not seen running.

   Changed:
   1. **Save buffer leak.** In `HeishaMon/src/control/glue/ctl_web.cpp` `save_step`, the write step at `content == 0` now frees the buffer and sets `userdata` to NULL right after `apply_arguments`, before the 301 goes out, as upstream's routes 100 and 110 do. The release at the close step and the one at the args step with no argument are kept, but only for a connection lost before the write. `docs/control/firmware.md` (the glue section and *Memory*) now says when the buffer is freed.
   2. **Unused `SAVE_NOTHING`.** It is replaced by `SAVE_EMPTY` ("The last save held no setting."). `render_item` writes it when a save happened and its report is empty, for example after a plain GET of `/savecontrol`. The doc bullet now says "a save that held no setting says so". With finding 3 fixed, "changed nothing" would no longer be true, so the old wording is gone.
   3. **Report and save rules now follow the Description.**
      - The page lists every name the setter accepted, an unchanged value included, plus every refused name.
      - MQTT's save-on-OK is back. `ctl_firmware_change_begin` in `ctl_firmware.cpp`/`.h` now takes `(bool topics, bool every_ok)`: `handle_setting` passes `every_ok` true, so every OK saves and applies as before this story. The page passes false, so it writes `/control.json` only when a value text changed, which is what the Description asks for.
      - In `firmware.md`, the persistence and MQTT paragraphs are back to save-on-OK, and the *Saving* paragraph is rewritten.
      - The decision entry in `docs/control/decisions.md` is rewritten (new title, reason and what was rejected). It rejects listing only changed settings and rejects changing MQTT's saving.
   4. **Hand-copied 16.** `HeishaMon/src/control/glue/ctl_settings.h` gains `CTL_SETTINGS_FIXED_NAMES` (16) and `CTL_SETTINGS_NAMES_MAX` (16 + 2 × `CTL_ROOMS_MAX`), and the comment on `ctl_settings_name_count` refers to them. In `ctl_settings.c`, a file-scope typedef with a negative array size stops the build if `FIXED_NAMES` and the header's count ever differ. `ctl_web.cpp` takes `SETTINGS_MAX` from `CTL_SETTINGS_NAMES_MAX`.

   Not done, open for the human:
   5. **Host tests for `ctl_web`.** Moving the collect, order and report logic into a pure `.c` module under `glue/` with a host test would need new files the Interface does not list, and the reviewer addressed this finding to the human. If wanted, it needs a criterion. Until then, everything `ctl_web` decides is only compiled by `make firmware`, never tested.

   Noticed, not touched: my earlier remarks still hold. Upstream's ESP8266 `WEBSERVER_BUFFER_SIZE` of 128 bytes limits a long URL-encoded topic in a form post. Upstream's own routes have the same gap on a dropped connection. The other upstream pages keep menus without the Control link.

   `check`: check: green
8. factory: started review as heishamon.
9. reviewer: Findings:

   1. `HeishaMon/src/control/glue/ctl_web.cpp` `keep_argument`: a long value can be saved as a fragment, silently. When a name with its URL-encoded value overflows upstream's buffer (128 bytes on the ESP8266), upstream's `webserver_parse_post` (`HeishaMon/src/common/webserver.cpp`, the `pos1+2 >= WEBSERVER_BUFFER_SIZE` branch) sends the value in several args steps under the same name. `keep_argument` finds the name with `argument_index` and overwrites `value`, so only the last fragment is applied. The form posts every topic on every save, so any save, even one that only changes `rooms`, replaces a long room or PV topic (many `/`, each sent as `%2F`) with its tail. The topic setter accepts that tail. The builder's note, "may not get through a form post", understates this: the setting is corrupted, not refused. Fix: when an argument arrives with the same name as the last kept one, append its value to that one, and mark it as not fitting if the total exceeds `CTL_SETTINGS_VALUE_MAX - 1`. Say in `docs/control/firmware.md` how a long field is handled.
   2. `HeishaMon/src/control/glue/ctl_firmware.h` (the comment on `ctl_firmware_change_end`) and `docs/control/decisions.md` (the new entry, the lines "choosing whether every `OK` counts as a change. A save from the" and "saving over MQTT only on a change, which changes a behaviour no one asked to change; comparing…"): the round-1 edits left lines far past the width of the files around them and broke the wrapping mid-sentence. Fix: rewrap both texts to the files' width.
   3. Story gap, for the human (this repeats finding 5 of the earlier review, which is still open): no acceptance criterion and no host test covers any rule of `ctl_web`. Those rules are: every other URI passes on untouched, settings apply in `ctl_settings_name_at` order (rooms first), the accepted and refused report, the buffer is freed, and, after finding 1 above, fragments are joined. All of this is Arduino C++ that only `make firmware` compiles. Both the leak in the previous round and finding 1 got through because of this. Option: a criterion that moves the collect, join, order and report logic into a pure `.c` module under `glue/`, with a host test.

   Checked without findings:
   - Criteria 1 and 2: `tests/control/test_html.c` covers the escapes, plain text, both sides of the exact-size boundary, the entity that would fit only in part, and the exact row and input forms with name and value escaped.
   - Criterion 4: the hook lines are as the story states. `HeishaMon.ino` has +2 lines and `htmlcode.h` +1, with nothing removed.
   - Round-1 fixes:
     - The save buffer is now freed at write step 0. Upstream clears `userdata` before the close step, and the frees at close and at the args step with no argument still cover a dropped connection.
     - `SAVE_EMPTY` is shown for a save that held no setting.
     - The report lists every OK, the MQTT path again saves on every OK, and the page saves only on a change, as the Description says.
     - `CTL_SETTINGS_NAMES_MAX` comes from `ctl_settings.h`, with a build-time check against `FIXED_NAMES`.
   - Route handling: upstream resets `route` and `userdata` per connection (`webserver_reset_client`). The METHOD step comes before the URI step, so claiming at the URI step overrides route 110, and skipping the METHOD step is correct.
   - Page chunking: state reaches `FINISHED` within the pass bound. `menuJS` and `webFooter` are sent once. A step that sends nothing comes only after the end. No section reaches 255 rows (at most 48 settings).
   - The malloc'd save buffer is the one allocation the Interface allows.

   Earlier tests: none changed. `tests/control/test_html.c` is the only test file in the diff, and it is new (status `A`).
10. factory: started doing as heishamon.
11. builder: Round 2: I fixed findings 1 and 2. Finding 3 is still open for the human. `make check` is green (`check: green`). Smoke test: `grep -c "control loop hook" HeishaMon/HeishaMon.ino` gives `8`, and `make firmware` prints both an `esp8266:` and an `esp32:` block. Among upstream's files, the diff stat still shows only `HeishaMon.ino` +2 and `htmlcode.h` +1. No board was run, so the joining of value pieces has only been compiled.

    Changed:
    1. **A long value arriving in pieces** (`HeishaMon/src/control/glue/ctl_web.cpp`):
       - `save_arguments` now remembers `last`, the argument the previous args step kept. `save_argument` now keeps its `length`.
       - `keep_argument` treats a step that repeats the previous step's name as the next piece of that value. A new name, or a name seen earlier but not just before, starts the value again.
       - The new helpers `start_value` and `append_value` do the work. When the joined value would reach `CTL_SETTINGS_VALUE_MAX` or holds a zero byte, the argument is marked as not fitting. It is then refused whole, and no later piece can make it fit again.
       - A dropped name (too long, or the buffer is full) resets `last`, so a later piece cannot attach to the wrong argument.
       - `docs/control/firmware.md` (*Saving*) now says how a long field is handled: upstream's 128-byte buffer on the ESP8266, pieces under one name, joined again, and refused whole if too long. The decision entry in `docs/control/decisions.md` records this. It also records the rejected alternative: cutting a joined value that is too long.
    2. **Wrapping:** I rewrapped the comment on `ctl_firmware_change_end` in `HeishaMon/src/control/glue/ctl_firmware.h` and the new entry in `docs/control/decisions.md` to the files' width of 100 columns.

    Decision: the join only applies to consecutive steps with the same name. The form never posts a name twice, so a name that comes back later starts its value again rather than adding to it. That is the same overwrite as before, and it cannot glue together two unrelated fields.

    Open for the human (finding 3, repeated from the earlier review): no acceptance criterion and no host test covers any rule of `ctl_web`. Those rules are: other URIs pass through, settings apply in `ctl_settings_name_at` order, the accepted and refused report, the buffer is freed, and value pieces are joined. All of this is Arduino C++ that only `make firmware` compiles. One option is a criterion that moves the collect, join, order and report logic into a pure `.c` module under `glue/` with a host test. That needs new files the Interface does not list, so I did not do it.

    Noticed, not touched:
    - `docs/control/firmware.md` has table rows longer than 100 columns from an earlier story.
    - Two lines in `ctl_web.cpp` run a few columns past 100. One is the HTML heading text and one is the `ctl_telemetry_entry` call.
    - My earlier remarks still hold: upstream's own routes have the same gap when a connection drops, and the other upstream pages keep menus without the Control link.

    `check`: check: green

    `check`: check: green
12. factory: started review as heishamon.
13. reviewer: Findings:

    1. `docs/control/firmware.md:281` (the *Saving* paragraph of *The control page*): this round's edit left a 144-column line ("setting, or holds a zero byte, is refused whole and never cut. When any value changed, the glue writes `/control.json` as above, and applies the"). The paragraph around it is wrapped at 100 columns. This is the same broken wrapping that finding 2 of the last review named in other files. The same check finds two new lines in `HeishaMon/src/control/glue/ctl_web.cpp` over 100 columns: line 179 (108, the `<h1>` of `PAGE_TOP`) and line 288 (101, the `ctl_telemetry_entry` call in `telemetry_row`). The builder noticed both and left them. Fix: rewrap that paragraph to 100 columns, and break both code lines.
    2. Story gap, for the human. This repeats finding 5 of the first review and finding 3 of the second. Both times the builder left it open, so it has never reached the human as a question. No acceptance criterion and no host test covers any rule of `ctl_web` (`HeishaMon/src/control/glue/ctl_web.cpp`), and these rules are untested:
       - every other URI passes on untouched;
       - settings apply in `ctl_settings_name_at` order, with `rooms` first;
       - the accepted and refused report, including `SAVE_EMPTY`;
       - the save buffer is freed at write step 0;
       - value pieces are joined, and refused whole when too long.

       All of this is Arduino C++ that only `make firmware` compiles. The leak in round 1 and the fragment corruption in round 2 both got through for this reason. Fix: the builder ends the next run with `question`, so the human can choose:
       - (a) add a criterion that moves the collect, join, order and report logic into a pure `.c` module under `glue/`, with a host test (my choice);
       - (b) accept the gap and say so in the Description.

    Checked without findings:
    - Round-2 fix for long values: I read upstream's `webserver_parse_post` (`HeishaMon/src/common/webserver.cpp`). A long value arrives as a run of args steps under the same name. That happens both in the branch that keeps a `%xx` together (`pos1+2 >= WEBSERVER_BUFFER_SIZE`) and in the full-buffer branch that moves only the name back. `keep_argument` joins consecutive same-name steps, so those pieces are joined. A piece is decoded before it is handed over, so `append_value` gets decoded bytes. A joined value that reaches `CTL_SETTINGS_VALUE_MAX`, or holds a zero byte, is marked as not fitting and stays refused. A dropped name resets `last`.
    - Round-2 fix for wrapping: the comment on `ctl_firmware_change_end` in `ctl_firmware.h` and the new entry in `docs/control/decisions.md` are now within 100 columns.
    - Criteria 1 and 2: `tests/control/test_html.c` covers the escapes, plain text, both sides of the exact-size boundary, the entity that would fit only in part, and the exact row and input forms with name and value escaped.
    - Criterion 4: among upstream's files, only `HeishaMon.ino` (+2, both ending in `// control loop hook`) and `htmlcode.h` (+1, after `/rules`) changed, with nothing removed.
    - Saving: MQTT still saves on every OK (`every_ok` true), and the page saves only on a change. The report lists every OK. `CTL_SETTINGS_NAMES_MAX` is a single constant, checked at build time.
    - Subscribing from a page save while MQTT is disconnected is harmless: PubSubClient refuses, and `ctl_firmware_subscribe` subscribes again on reconnect.
    - Page chunking: the loop is bounded, and a partial item is never sent (length is counted only for whole items). `menuJS` and `webFooter` are sent once.
    - Embedded safety: every `strcpy` source is length-checked against `CTL_SETTINGS_NAME_MAX`. The malloc'd save buffer is the one allocation the Interface allows.

    Earlier tests: none changed. `git diff --name-status develop...HEAD -- tests` shows only `A tests/control/test_html.c`.
14. factory: started doing as heishamon.
15. builder: Round 3: I fixed finding 1. Finding 2 is a question for the human, as the reviewer asked, so I am stopping here and not handing forward.

    Fixed (finding 1, wrapping):
    - `docs/control/firmware.md`: I rewrapped the *Saving* paragraph of *The control page* to 100 columns. The 144-column line is gone. Lines 103, 133 and 134 of that file are still over 100, but they are table rows from an earlier story and are not in this story's diff, so I left them.
    - `HeishaMon/src/control/glue/ctl_web.cpp`: I broke the `<h1>` of `PAGE_TOP` before "Control loop". This replaces a space with a newline inside HTML text, so the page renders the same. I also broke the `ctl_telemetry_entry` call in `telemetry_row`. No line in the story's code, tests or docs now runs past 100 columns.

    Checks: `make check` is green (lint, test, device, firmware, `check: green`). Smoke test: `grep -c "control loop hook" HeishaMon/HeishaMon.ino` gives `8`, and `make firmware | grep -c -E '^(esp8266|esp32):'` gives `2`. Among upstream's files, the diff stat still shows only `HeishaMon.ino` +2 and `htmlcode.h` +1.

    Question for the human (finding 2, raised in all three reviews): none of `ctl_web`'s rules has an acceptance criterion or a host test. Those rules are:
    - every other URI passes through untouched;
    - settings apply in `ctl_settings_name_at` order, `rooms` first;
    - the page reports what was accepted and refused, including the empty-save line;
    - the save buffer is freed at write step 0;
    - a long value that arrives in pieces is joined, and refused whole if too long.

    All of this lives in `HeishaMon/src/control/glue/ctl_web.cpp`, which only `make firmware` compiles. The leak in round 1 and the fragment corruption in round 2 both got through because nothing tests this code. Options:
    - (a) Add a criterion: move the collect, join, order and report logic into a new pure C module, for example `HeishaMon/src/control/glue/ctl_form.h`/`ctl_form.c`, with a host test `tests/control/test_form.c`. `ctl_web.cpp` would keep only the upstream steps (routes, chunks, redirect) and call that module. The Interface would list the new files and functions. The firmware behaviour stays the same; only the place where the logic lives moves.
    - (b) Accept the gap. The Description would say that `ctl_web`'s rules are checked only by compiling and by reading the code.

    I would choose (a): it is the only way the next change to the save logic gets tested before it reaches a board. If you choose (a), please name the new files, or let me name them, and I will add them to the Interface and as a criterion before the two closing ones.
16. 2026-11-12 human: (a), with your names: `HeishaMon/src/control/glue/ctl_form.h`, `ctl_form.c` and `tests/control/test_form.c`. The collect, join, order, apply and report logic moves there; `ctl_web.cpp` keeps only the upstream steps. The new criterion: the host test covers a save in browser order with `rooms` raised and its new room fields, a value that arrives in pieces, a value too long when joined, an unknown name, and the report text with accepted, refused and the empty save. This answer is part of the Description.

    — Kumpel, for the human who owns this project
    <!-- kumpel -->
