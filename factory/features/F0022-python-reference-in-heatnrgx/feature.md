# The Python reference lives only in heatnrgx

## Goal

The fork carries a copy of the Python reference (`docs/control/python-reference/`, 1,073 lines) and its API manual (`docs/control/reference.md`, 816 lines). The original lives in the owner's separate, private repository heatnrgx, which also holds the house simulator and exports the golden traces. Two copies drift: the fork's copy has no integral term, heatnrgx's has. What guards the C core is not the copy but the golden traces, which the C core must replay bit for bit. The owner decided: one source of truth in heatnrgx; the fork keeps the traces. Done when the story is merged, `make check` is green, and no document of the fork points to a file that is gone.

## Scope

- Remove `docs/control/python-reference/` and `docs/control/reference.md`.
- Point every mention of them in the fork's documents, comments and tests to the golden traces, `core.md`, `golden-traces.md` or the reference in heatnrgx.

## Out of scope

- The golden traces and the trace test: they stay as they are.
- The core's code and behaviour.
- `CLAUDE.md` and the `Makefile`: the human changed `CLAUDE.md` before this story.

## Stories

- S0001: Remove the Python reference copy

## The human's changes

Before S0001: `CLAUDE.md` no longer names `docs/control/python-reference/`; it says the golden traces come from the Python reference in heatnrgx, and that a change to the core's behaviour comes with new traces the human exports from heatnrgx. Done in commit "CLAUDE.md: the Python reference lives in heatnrgx".

## Decisions taken for the human

1. heatnrgx is private, so the fork names it as the owner's separate repository and does not link to it.
2. The phrase "the Python reference" may stay in comments where it explains why the C code computes as it does; only paths to the removed files go.
3. The entries in `decisions.md` stay as history; a path in them to a removed file is not changed.
