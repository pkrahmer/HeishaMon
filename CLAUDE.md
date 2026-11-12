# HeishaMon, with a control core

A fork of HeishaMon (github.com/heishamon/HeishaMon), the Arduino firmware that links a Panasonic Aquarea heat pump to MQTT. The fork adds a control core that runs on the board. kumpel-factory builds the fork's own code; the pipeline's rules reach every agent in its instructions, so this file holds only what is true of this project.

`AGENTS.md` describes upstream's code: the two boards, the data flow, the decode and command tables, the rules engine. Read it before you read or call an upstream file.

## Language
- Everything the fork adds is English: code, comments, documents, work items, commit messages.

## Architecture
- **The fork's own code lives in three folders**: `HeishaMon/src/control/` (code), `tests/control/` (host tests) and `docs/control/` (documents). Every other file is upstream's. An agent never changes an upstream file, with one exception: the hook lines in `HeishaMon/HeishaMon.ino` that a story names one by one, each a single call into `HeishaMon/src/control/glue/` (or an `#include` of its headers), and the one menu link to `/control` in `HeishaMon/htmlcode.h`, so that a rebase onto upstream stays small. Arduino compiles everything under `HeishaMon/src/` into the firmware, so every file under `HeishaMon/src/control/` must also build for both boards.
- **The core** is `HeishaMon/src/control/core/`: plain C99 that takes values and returns values. It has no input or output, no clock, no logger and no global state that a caller cannot see. It includes only the headers of its own folder and `math.h`, `stdbool.h`, `stdint.h` and `stddef.h`. Time reaches it as a number of elapsed seconds in an argument.
- **Around the core**, the other `.c` files under `HeishaMon/src/control/` are pure too: they take values and return values, and they compile on the host. They may include the core's headers; the core never includes theirs.
- **Code that needs the Arduino framework or an upstream header is a `.cpp` file**, never a `.c` file. Every `.c` file under `HeishaMon/src/control/` must compile on the host as strict C99.
- **No dynamic memory**: no `malloc`, no variable-length array, no recursion. Every list has a compile-time maximum, and every loop has a bound.
- **Numbers are `double`** in the core, never `float`, and every expression keeps the operand order of the Python reference (`docs/control/python-reference/`): the golden traces are compared bit for bit, and the build forbids fused multiply-add (`-ffp-contract=off`). The core treats a NaN or an infinite value as the Python reference does; around the core, a value from outside is checked with `isfinite` and against its range before it is used.
- **Names**: every public name of the control code starts with `ctl_` (functions and types) or `CTL_` (macros and enum values). A function that is not part of a header is `static`.
- **Errors are values**: a function that can refuse returns a status, and the caller checks it. Nothing aborts or exits.

## Tests
- A test is a C99 program `tests/control/test_<subject>.c` with its own `main`. It returns 0 when every check passed, and it prints one line per failed check with the file, the line and the two values. `make test` builds each one with every `.c` file of the control code and runs it from the root of the checkout.
- A `.c` file under `tests/control/` whose name does not start with `test_` is a shared helper and is linked into every test program. Before you write a helper, look for one there.
- The golden traces are `tests/control/data/*.trace.gz`, exported from the Python reference; `docs/control/golden-traces.md` is their format. Nobody edits them. `make test` unpacks them into a temporary folder and names it in the environment variable `CTL_TRACES`; a test reads `$CTL_TRACES/<name>.trace`. Other test data lives beside them and is read by its path from the root of the checkout.
- Nothing may be faked inside the core: a test calls the real functions with values. Time is a number the test passes in.
- A test another story wrote is changed only when the story names it with the human's word.
- The device test lives in `tests/control/device/`: the sketch `replay/replay.ino`, which serves the core over a serial line on a D1 mini (ESP8266), `host_server.c`, which serves it the same way over standard input and output on the host, and `replay.py`, which feeds a trace to either and compares. The line protocol itself is C under `HeishaMon/src/control/wire/`, pure like the core. The sketch only moves lines between the serial port and that code.
- Upstream's two host suites (`Examples/Rules/run_tests.sh` and `tests/modbus/run_tests.py`) run in `make test` too. They are upstream's and stay as they are.

## Docs
- `docs/control/` holds the fork's own documents. `docs/control/decisions.md` is the decision log: one entry per decision, with the date, the decision, the reason and what was rejected.
- Upstream's documents (the `README` files and the Markdown files at the root) stay as they are.
- Every header under `HeishaMon/src/control/` opens with a comment that says what the module is for. Every function a header declares has a comment that says what it returns and what it refuses. Comments say why, never what.

## Checks
- `make check` must be green before any stage reports done. It runs `make lint`, `make test`, `make device` and `make firmware` and ends with `check: green`. Each target ends with the line `<target>: green (<seconds> s)`.
- `make lint` compiles every `.c` file under `HeishaMon/src/control/` with `gcc -std=c99 -Wall -Wextra -Werror -pedantic -ffp-contract=off -fsyntax-only`, and it refuses an include in the core that the *Architecture* does not allow.
- `make test` runs upstream's two host suites, then every test program of the control code, then the device replay on the host: `host_server.c` built for the host and fed two traces by `replay.py`, then every `tests/control/device/test_*.py` with `python3` from the root of the checkout. Such a test finds the control code's `.c` files in the environment variable `CTL_SOURCES` (separated by white space) and a scratch folder that `make test` removes in `CTL_BUILD`; it uses the standard library only, exits with 0 when it passed, and prints one line per failure.
- `make device` compiles every sketch under `tests/control/device/` (a folder holding a `.ino` of its own name) for the D1 mini with arduino-cli, each folder under `HeishaMon/src/control/` as a library, and prints the memory it uses. A library folder is flat: a `.c` file includes a header of another folder by a relative path (`"../core/ctl_controller.h"`). Nothing is flashed: no stage has a board. `make device-replay PORT=<port>` runs the replay against a flashed board, by hand on the machine it is plugged into.
- `make firmware` compiles `HeishaMon/HeishaMon.ino`, with everything under `HeishaMon/src/`, for the D1 mini and the ESP32-S3 with the options of upstream's `scripts/build_*.sh`, and prints each board's memory lines. It is the only check that compiles a `.cpp` file of the control code. It takes minutes: give it the shell tool's largest timeout.

## Not without asking (end the stage with `question`)
- A change to any file outside the three folders, a new library, a change to `factory/`, the `Makefile` or this file, a change to a test you did not write, and anything that would send a command to the heat pump more often than the story says.
