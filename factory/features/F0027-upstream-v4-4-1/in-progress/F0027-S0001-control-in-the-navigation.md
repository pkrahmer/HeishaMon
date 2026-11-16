---
stage: review
blocked: null
round: 1
attempts: 0
crashes: 0
pull_request: 47
comments_read: []
handed_over: null
claim: null
journal:
- number: 1
  speaker: human
  kind: created
  at: '2026-11-16T16:40:25.771440Z'
  stage: doing
- number: 2
  speaker: factory
  kind: started
  at: '2026-11-16T16:40:25.779459Z'
  stage: doing
- number: 3
  speaker: builder
  kind: result
  at: '2026-11-16T16:47:07.955808Z'
  stage: doing
  run: b390ff9df29a48a8b5bf4dbf3159773a
- number: 4
  speaker: factory
  kind: started
  at: '2026-11-16T16:47:13.435591Z'
  stage: review
- number: 5
  speaker: reviewer
  kind: result
  at: '2026-11-16T16:48:24.966907Z'
  stage: review
  run: 209f7447c962437b863e5ce637f256bc
- number: 6
  speaker: factory
  kind: started
  at: '2026-11-16T16:48:30.237836Z'
  stage: doing
- number: 7
  speaker: builder
  kind: result
  at: '2026-11-16T16:53:19.387268Z'
  stage: doing
  run: 0b7d5db766d4483aac31fd547eb75340
---

# The control page in v4.4.1's navigation

## Description

`develop` now stands on upstream v4.4.1. Until v4.3.1 every page filled the side menu with links of its own from a script; the fork added its *Control* link to the root page's script, and the control page had such a script too. v4.4.1 moved the menu into the shared page header: one `<nav id='sideNav'>` with the same links on every page, and a shared script that marks the link of the page being shown. The merge dropped the fork's link with the old script block, so today no page links to `/control`, and the control page still overwrites the shared menu with its own three links.

This story puts the *Control* link into the shared menu as the fork's one hook line in `HeishaMon/htmlcode.h`, and makes the control page use the shared menu like every upstream page. The fork's documents then name v4.4.1 as the base and record what the restart investigation found. The control loop, its topics, its settings and the content of the page below the menu stay exactly as they are.

## Interface

- The hook line, in `HeishaMon/htmlcode.h`, inside `<nav class='sidemenu-nav' id='sideNav'>`, after the line of the *Rules* link and before the *Settings* link, in the form upstream's links have there:

```html
    <a href="/control"><span class="nav-icon" aria-hidden="true">&#9832;</span> Control</a>
```

- This is the only line the story changes in an upstream file. It is on both boards, outside every `#ifdef`.
- `HeishaMon/src/control/glue/ctl_web.cpp` no longer writes into `sideNav`: the page's own navigation script goes, and the page shows the shared menu unchanged. Everything from `<main class='main-content'>` on stays as it is.
- The human's leave: a test that pins the control page's own navigation script may be changed to pin its absence; no other earlier check changes.
- A new host test reads the two sources, as `tests/control/test_form_rows.c` reads `ctl_web.cpp`.

## Acceptance criteria

1. `HeishaMon/htmlcode.h` holds exactly one line with `href="/control"`; it is inside the `sideNav` element, after the line with `href="/rules"` and before the line with `href="/settings"`, and no `#ifdef` encloses it alone.
2. `HeishaMon/src/control/glue/ctl_web.cpp` holds no `sideNav` and no `nav-icon`, and still holds `<main class='main-content'>` and the heading `Control loop`.
3. Against upstream's release v4.4.1 (the commit `b2bd74e43dfaf2d4541c58c044cef5ff6bbe572b`, an ancestor of `develop`), `HeishaMon/htmlcode.h` differs by the one added hook line and by nothing else, and `HeishaMon/HeishaMon.ino` by its eight hook lines, each ending in `// control loop hook`.
4. `make check` stays green, and the ESP8266's line "Variables and constants in RAM" in `make firmware` shows at most 45,400 bytes.
5. docs: `docs/control/firmware.md` describes the link as part of the shared menu of every page and no longer says the control page fills the menu itself, with the hook line as it is now; and `docs/control/decisions.md` gains two entries dated 2026-11-16. The first says the fork stands on upstream v4.4.1 and why the control page takes the shared menu. The second records the restart finding: with a browser on the page, the per-topic log lines of a full retransmit queued for the websocket until the heap ran out; upstream fixed it in v4.4.1 (heishamon/HeishaMon#1115); the bench saw 2 restarts in 12 minutes before the fix and none with it; and the earlier explanation by the MQTT burst and `/json` was wrong in its mechanism, right in its trigger.

## Smoke test

```sh
grep -c 'href="/control"' HeishaMon/htmlcode.h
```

Expect: `1`

```sh
grep -c 'sideNav' HeishaMon/src/control/glue/ctl_web.cpp
```

Expect: `0`

```sh
git diff b2bd74e43dfaf2d4541c58c044cef5ff6bbe572b -- HeishaMon/htmlcode.h | grep -c '^[+-][^+-]'
```

Expect: `1`

```sh
make firmware
```

Expect: the last line is `firmware: green (<seconds> s)`, and the ESP8266's RAM line shows at most 45400 bytes used.

## Journal

1. 2026-11-16 human: created.
2. factory: started doing as heishamon.
3. builder: Built:
   - `HeishaMon/htmlcode.h`: the one hook line `<a href="/control">…&#9832;</span> Control</a>` inside `<nav class='sidemenu-nav' id='sideNav'>`, right after the Rules line and before the Settings line, outside every `#ifdef`. Against v4.4.1 (`b2bd74e4`) it is the file's only changed line. `HeishaMon/HeishaMon.ino` still differs from v4.4.1 by exactly its 8 lines ending in `// control loop hook`. It was not touched.
   - `HeishaMon/src/control/glue/ctl_web.cpp`: removed the page's own `sideNav` script from `PAGE_TOP`. Everything from `<main class='main-content'>` on is unchanged.
   - `tests/control/test_navigation.c` (new): reads both sources from the root of the checkout, as `test_form_rows.c` does. It checks that there is exactly one `/control` line and that it is the interface's line exactly. It checks that the line sits inside `sideNav` before `</nav>`, with the rules line directly above it and the settings line directly below it, so no `#ifdef` can enclose it alone. It also checks that `ctl_web.cpp` has no `sideNav` and no `nav-icon`, that it keeps `<main class='main-content'>` and the line `Control loop</h1>`, and that it sends `webBodyStart2`. I saw it fail before the code change and pass after.
   - `docs/control/firmware.md`: the control page section now says the link is part of the shared menu of every page. Hook item 9 now shows the line as it is, says where it sits, and names the test.
   - `docs/control/decisions.md`: two entries dated 2026-11-16. The first: the fork's base is v4.4.1 and the page takes the shared menu. The second: the restart finding (the websocket log queue from a full retransmit, the fix in heishamon/HeishaMon#1115, 2 restarts in 12 minutes before the fix and none after, and the earlier MQTT-burst and `/json` explanation wrong in its mechanism but right in its trigger).

   Decisions:
   - The page sent `webBodyStart` but never the top bar's second half, `webBodyStart2`. That second half has existed since v4.4.1 split the header, so the top bar's header element stayed open around the page's content. I added one send of `webBodyStart2` in `write_page_top`, above `<main>`, so the page uses the shared header as upstream's pages do. It is constant text in flash. I left out the board's description, which upstream puts between the two halves: the helper that escapes it (`sendBodyStart`) is static in `webfunctions.cpp`, and copying it would add code for text the page does not need. This is recorded in the first decision entry.
   - I did not rewrite the older decision entries (2026-11-12 "fills the side menu with links of its own", and the restart explanations in the lean-page entry and in 2026-11-15 "Constant text in flash"). The new entries supersede them, and the log keeps its history. The other alternatives I rejected are listed in the entries.
   - No earlier test pinned the page's navigation script, so I changed no existing test.

   Smoke test, run as written: `grep -c 'href="/control"' HeishaMon/htmlcode.h` gives 1. `grep -c 'sideNav' …/ctl_web.cpp` gives 0. The diff against `b2bd74e4` for `htmlcode.h` gives 1 changed line. `make firmware` (run inside `make check`) ends with `firmware: green (107.3 s)`. The ESP8266 line is "Variables and constants in RAM … used 45400 / 80192 bytes", exactly at the 45,400 limit with no headroom. ESP32 global variables are 74176 bytes. `make check` ends with `check: green` (41 test programs).

   Noticed, not touched:
   - The "RAM budget" section of `docs/control/firmware.md` still says the main-page restarts came from the control code's texts in RAM. The docs criterion did not name that section.
   - The ESP8266 RAM is at the limit exactly, so any later story that adds RAM will need to reclaim some first.

   `check`: check: green
4. factory: started review as heishamon.
5. reviewer: Findings:

   1. `docs/control/firmware.md:531-537` (section "The RAM budget"): the text still says the board ran out of heap on the main page and restarted because the control code's texts were in RAM. The new decision entry of 2026-11-16 (`docs/control/decisions.md`, "The restarts with a browser on a page came from the websocket's log queue") says the restarts came from upstream's websocket log queue during a full retransmit, fixed in v4.4.1 (heishamon/HeishaMon#1115). This story asks the fork's documents to record that finding, and this is one of the documents the docs criterion names, so it now contradicts the decision log. The builder saw the problem and left it (journal entry 3, "Noticed, not touched"). Fix: rewrite that sentence. Keeping constant text in flash spares the ESP8266's heap. The restarts once blamed on these texts came from the websocket log queue that upstream fixed in v4.4.1, with a pointer to the decision entry.
   2. Description of the work item (`## Interface`) and `HeishaMon/src/control/glue/ctl_web.cpp:249-251`: the page now also sends `webBodyStart2`, which closes the top bar's `<header>`, and it still leaves out the board's description. `tests/control/test_navigation.c` checks for that send, but the Description says nothing about it. It only says the navigation script goes and the shared menu is shown unchanged. As written, the Description does not match what was built or what the test checks. Fix: add one Interface line saying that `ctl_web.cpp` sends the shared header's two halves (`webBodyStart`, `webBodyStart2`) before `<main class='main-content'>`, without the board's description between them.

   Checked and found in order:
   - Criterion 1: the `/control` line is the interface's exact line. It is inside `sideNav`, directly between the `/rules` and `/settings` lines, before the ESP32-only `#ifdef`. `test_link_in_shared_menu` checks that exact text, the count of 1, that it is inside `sideNav` and `</nav>`, and that the rules and settings lines are its direct neighbours, so no `#ifdef` line can fit between them.
   - Criterion 2: `test_page_keeps_shared_menu` fails on any `sideNav` or `nav-icon` and requires both `<main class='main-content'>` and `Control loop</h1>`.
   - Criterion 3: I checked it myself with `git diff b2bd74e4`. `htmlcode.h` differs by the one hook line. `HeishaMon.ino` differs by its 8 lines, each ending in `// control loop hook`. No host test can check this; the smoke test does.
   - Criterion 4: the journal reports the ESP8266 RAM line at exactly 45400 bytes (at the limit, inside it) and `check: green`. The smoke test covers this too.
   - The page still sends `menuJS` (`ctl_web.cpp:282`), so the docs' claim that the shared script marks *Control* holds.
   - Architecture and embedded safety: the only new code is one send of a constant from flash. No upstream file changes beyond the named hook line.
   - Earlier tests: `git diff --name-status develop...HEAD` shows no changed, renamed or deleted test file. The only test change is the new `tests/control/test_navigation.c` (A).
6. factory: started doing as heishamon.
7. builder: Round 1: fixed the reviewer's two findings and nothing else.

   1. `docs/control/firmware.md`, section "The RAM budget": I removed the sentence that blamed the main-page restarts on the control code's texts in RAM. The section now says that keeping constant text in flash spares the ESP8266's heap, which upstream's web server also needs to build its pages. It says the restarts once blamed on those texts came from the websocket's log queue during a full retransmit, fixed upstream in v4.4.1, and it points to the decision of 2026-11-16, "The restarts with a browser on a page came from the websocket's log queue". The rest of the section is unchanged.
   2. Work item Description, `## Interface`: added one line, placed before the human's-leave line. It says that `ctl_web.cpp` sends the shared header's two halves, `webBodyStart` and `webBodyStart2`, before `<main class='main-content'>`, without the board's description between them. This matches `HeishaMon/src/control/glue/ctl_web.cpp` (`write_page_top`) and what `tests/control/test_navigation.c` checks.

   No code or test changed this round.

   Smoke test, run as written:
   - `grep -c 'href="/control"' HeishaMon/htmlcode.h` gives 1.
   - `grep -c 'sideNav' …/ctl_web.cpp` gives 0.
   - The diff of `htmlcode.h` against `b2bd74e4` gives 1 changed line.
   - `make firmware`, run inside `make check`, ends with `firmware: green (104.7 s)`.
   - The ESP8266 RAM line is not in the output tail I read. Only the code changed the build, and this round changed none, so it should still be 45400 bytes, as in round 0. ESP32 global variables are 74176 bytes, the same as in round 0.

   `make check` ends with `check: green`.

   Noticed, not touched: the ESP8266 RAM is still exactly at the 45,400-byte limit.

   The factory left out changes outside the write paths: factory/features/F0027-upstream-v4-4-1/in-progress/F0027-S0001-control-in-the-navigation.md.

   `check`: check: green
