# HeishaMon, with a control core

A fork of HeishaMon (github.com/heishamon/HeishaMon), the Arduino firmware that links a Panasonic Aquarea heat pump to MQTT. The fork adds a control core that runs on the board. kumpel-factory builds the fork's own code; the pipeline's rules reach every agent in its instructions, so this file holds only what is true of this project.

`AGENTS.md` describes upstream's code: the two boards, the data flow, the decode and command tables, the rules engine. Read it before you read or call an upstream file.

## Language
- Everything the fork adds is English: code, comments, documents, work items, commit messages.

## Architecture
- **The fork's own code lives in three folders**: `HeishaMon/src/control/` (code), `tests/control/` (host tests) and `docs/control/` (documents). Every other file is upstream's. An agent never changes an upstream file; the hook lines in upstream files are the human's, so that a rebase onto upstream stays small.
- **The core** is `HeishaMon/src/control/core/`: plain C99 that takes values and returns values. It has no input or output, no clock, no logger and no global state that a caller cannot see. It includes only the headers of its own folder and `math.h`, `stdbool.h`, `stdint.h` and `stddef.h`. Time reaches it as a number of elapsed seconds in an argument.
- **Around the core**, the other `.c` files under `HeishaMon/src/control/` are pure too: they take values and return values, and they compile on the host. They may include the core's headers; the core never includes theirs.
- **Code that needs the Arduino framework or an upstream header is a `.cpp` file**, never a `.c` file. Every `.c` file under `HeishaMon/src/control/` must compile on the host as strict C99.
- **No dynamic memory**: no `malloc`, no variable-length array, no recursion. Every list has a compile-time maximum, and every loop has a bound.
- **Floats are `float`**, not `double`, and a literal carries the suffix `f`. A value from outside the core is checked with `isfinite` and against its range before it is used.
- **Names**: every public name of the control code starts with `ctl_` (functions and types) or `CTL_` (macros and enum values). A function that is not part of a header is `static`.
- **Errors are values**: a function that can refuse returns a status, and the caller checks it. Nothing aborts or exits.

## Tests
- A test is a C99 program `tests/control/test_<subject>.c` with its own `main`. It returns 0 when every check passed, and it prints one line per failed check with the file, the line and the two values. `make test` builds each one with every `.c` file of the control code and runs it from the root of the checkout.
- A `.c` file under `tests/control/` whose name does not start with `test_` is a shared helper and is linked into every test program. Before you write a helper, look for one there.
- Test data (a trace, a table of cases) lives under `tests/control/data/`. A test reads it by its path from the root of the checkout.
- Nothing may be faked inside the core: a test calls the real functions with values. Time is a number the test passes in.
- A test another story wrote is changed only when the story names it with the human's word.
- Upstream's two host suites (`Examples/Rules/run_tests.sh` and `tests/modbus/run_tests.py`) run in `make test` too. They are upstream's and stay as they are.

## Docs
- `docs/control/` holds the fork's own documents. `docs/control/decisions.md` is the decision log: one entry per decision, with the date, the decision, the reason and what was rejected.
- Upstream's documents (the `README` files and the Markdown files at the root) stay as they are.
- Every header under `HeishaMon/src/control/` opens with a comment that says what the module is for. Every function a header declares has a comment that says what it returns and what it refuses. Comments say why, never what.

## Checks
- `make check` must be green before any stage reports done. It runs `make lint` and `make test` and ends with `check: green`. Each target ends with the line `<target>: green (<seconds> s)`.
- `make lint` compiles every `.c` file under `HeishaMon/src/control/` with `gcc -std=c99 -Wall -Wextra -Werror -pedantic -fsyntax-only`, and it refuses an include in the core that the *Architecture* does not allow.
- `make test` runs upstream's two host suites and then every test program of the control code.
- No check builds the firmware for the boards yet. A `.cpp` file is therefore compiled by no check; a story that adds one says how the human builds it.

## Not without asking (end the stage with `question`)
- A change to any file outside the three folders, a new library, a change to `factory/`, the `Makefile` or this file, a change to a test you did not write, and anything that would send a command to the heat pump more often than the story says.
