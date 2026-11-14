# Use the neutral trace name

## Description

**Why.** The golden trace `barbarastr_march` is named after the owner's street, and the fork is public. The human added the same trace under the neutral name `house_march` (`tests/control/data/house_march.trace.gz`); apart from the name in its header lines, its data is identical line by line.

**What changes.** The trace test (`tests/control/test_traces.c`) and the integral test (`tests/control/test_integral.c`) read `house_march` instead of `barbarastr_march`, with the same call count, 129,493. `tests/control/data/barbarastr_march.trace.gz` is removed. `docs/control/core.md` and `docs/control/golden-traces.md` name the trace `house_march`; their description of it stays as it is.

**What stays.** Every other trace, every check, the core, and the entries of `docs/control/decisions.md`.

## Interface

- By the human's word, two tests another story wrote change: in `tests/control/test_traces.c` and `tests/control/test_integral.c`, only the name `barbarastr_march` becomes `house_march`. Nothing else in them changes.
- Removed: `tests/control/data/barbarastr_march.trace.gz`.
- No upstream file changes, and no change to the `Makefile` or `CLAUDE.md`.

## Acceptance criteria

1. `git grep -n -i barbarastr -- . ':!factory' ':!docs/control/decisions.md'` prints nothing, and `git ls-files tests/control/data | grep -c -i barbara` prints `0`.
2. `make test` prints the trace test's summary line with the same number of files, calls and 0 mismatches as on `develop`.
3. `git diff develop -- tests/control/test_traces.c tests/control/test_integral.c | grep -E '^[-+][^-+]' | grep -v -E 'barbarastr_march|house_march'` prints nothing.
4. `git diff develop --stat -- HeishaMon` prints nothing.
5. `make check` stays green.
6. docs: `docs/control/core.md` and `docs/control/golden-traces.md` use the new name; `docs/control/decisions.md` gets one entry: the March trace renamed to a neutral name, because the fork is public and the old name was the owner's street; rejected: rewriting the history.

## Smoke test

```sh
make test 2>&1 | grep -E '^control: [0-9]+ test programs passed$' | wc -l
```

Expect: `1`

## Journal
