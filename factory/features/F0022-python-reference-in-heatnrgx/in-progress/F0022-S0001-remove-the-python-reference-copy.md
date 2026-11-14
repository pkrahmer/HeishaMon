# Remove the Python reference copy

## Description

**Why.** The fork carries a copy of the Python reference and its API manual, while the original lives in the owner's private repository heatnrgx, which also exports the golden traces. The copies already differ (heatnrgx has the integral term, the fork's copy does not). The golden traces, replayed bit for bit by `tests/control/test_traces.c`, are what guards the C core. The owner decided to keep the Python reference only in heatnrgx.

**What goes.** The folder `docs/control/python-reference/` with every file in it, and `docs/control/reference.md`.

**What changes.** Every place in the fork's documents (`docs/control/`), the control code's comments (`HeishaMon/src/control/`) and the tests (`tests/control/`) that names one of the removed files by its path is reworded: it points to the golden traces, to `core.md` or `golden-traces.md`, or names "the Python reference in heatnrgx" without a link. The phrase "the Python reference" may stay where it explains why the C code computes as it does. `docs/control/core.md` says in its opening where the reference lives now. The entries of `docs/control/decisions.md` stay as they were written.

**What stays.** The golden traces, every test, the core's code and its behaviour. No line of C code changes except comments.

## Interface

- Removed: `docs/control/python-reference/`, `docs/control/reference.md`.
- Comments only in `HeishaMon/src/control/` and `tests/control/`; no test's checks change.
- No upstream file changes, no hook line changes, no change to the `Makefile` or `CLAUDE.md`.

## Acceptance criteria

1. `git ls-files docs/control/python-reference docs/control/reference.md` prints nothing.
2. `git grep -n -E "python-reference|reference\.md" -- HeishaMon tests docs/control ':!docs/control/decisions.md'` prints nothing.
3. Every golden trace replays bit for bit in `tests/control/test_traces.c`, as before.
4. `git diff develop -- HeishaMon/src/control tests/control | grep -E '^[-+][^-+]' | grep -v -E '^[-+][[:space:]]*(/?\*|//|\*)'` prints nothing: only comment lines change in code and tests.
5. `git diff develop --stat -- HeishaMon ':!HeishaMon/src'` shows no upstream file changed.
6. `make check` stays green.
7. docs: `docs/control/core.md` says in its opening that the Python reference lives in the owner's separate repository heatnrgx and that the golden traces exported from it are the reference the core is held to; `docs/control/golden-traces.md` says the same where it names their source; `docs/control/decisions.md` gets one entry: the copy of the Python reference removed from the fork, because two copies drift and the traces are the guard; rejected: keeping the copy in sync by hand.

## Smoke test

```sh
make test 2>&1 | grep -E '^control: [0-9]+ test programs passed$' | wc -l
```

Expect: `1`

## Journal
