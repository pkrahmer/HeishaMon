---
stage: doing
blocked: null
round: 0
attempts: 0
crashes: 0
pull_request: null
comments_read: []
handed_over: null
claim:
  instance: heishamon
  stage: doing
  started_at: '2026-11-12T07:11:04.237422Z'
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
