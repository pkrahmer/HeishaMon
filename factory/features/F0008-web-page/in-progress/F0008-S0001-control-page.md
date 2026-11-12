---
stage: review
blocked: null
round: 0
attempts: 0
crashes: 0
pull_request: 22
comments_read: []
handed_over: null
claim:
  instance: heishamon
  stage: review
  started_at: '2026-11-12T07:28:54.107106Z'
  lease_duration: 60
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
