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
