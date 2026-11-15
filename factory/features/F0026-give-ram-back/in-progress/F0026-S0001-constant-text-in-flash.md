# Constant text in flash

## Description

**Why.** On the ESP8266, a `const char` text lives in RAM unless it is placed in flash. The control code keeps about 4.0 KB of such text in RAM: topic and subtopic names, setting names, log texts, the control page's snippets, the reset reason words and the type names. Upstream's web server needs that RAM when it builds the main page; without it, the board restarts. This story moves the control code's constant text to flash on the ESP8266, as upstream does with `PSTR`, `F()` and `PROGMEM`, without changing any text or behaviour.

**How it stays pure.** The `.c` files must still compile on the host as strict C99. One small header in the control code provides what is needed: on the ESP8266 the flash attribute and flash-safe reading, on the host and the ESP32 plain `const` data and ordinary reading. Text read from flash is copied into the caller's buffer before it is used, as the glue already does with `ctl_text`.

**What stays.** Every text, topic, value, log line and page byte is exactly as before; the core's behaviour and the golden traces are untouched.

## Interface

- One new header under `HeishaMon/src/control/` with the flash macros and their host fallbacks; its use in the files that hold the constant text.
- No test another story wrote changes, except where a test reads a table directly and must now read it through the new accessor; such a change keeps the check's meaning and is named in the result.
- No upstream file changes, and no hook line changes.

## Acceptance criteria

1. `make firmware` builds both boards, and the D1 mini's global RAM ("Variables and constants in RAM") is at least 2,500 bytes below `develop` at this story's start. Report both numbers.
2. A host test renders the control page with a fixed settings set and compares it byte for byte with the rendering on `develop` (a reference captured into the test), and the telemetry test's topic and value texts stay as they are.
3. Every golden trace replays bit for bit.
4. `git diff develop --stat -- HeishaMon ':!HeishaMon/src'` shows no upstream file changed.
5. `make check` stays green.
6. docs: `docs/control/firmware.md` gets a short section on the RAM budget: why constant text lives in flash on the ESP8266, and the rule for new text; `docs/control/decisions.md` gets one entry: constant text in flash, because the board restarted when the main page ran out of heap; rejected: leaving it in RAM.

## Smoke test

```sh
make test 2>&1 | grep -E '^control: [0-9]+ test programs passed$' | wc -l
```

Expect: `1`

## Journal
