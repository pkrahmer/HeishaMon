# A neutral name for the March trace

## Goal

One golden trace is named after the street the owner lives on (`barbarastr_march`), in its file name, its first lines and in the fork's tests and documents. The fork is public, so the name is a hint to where the owner lives. The owner decided to give it a neutral name. The human exported the same trace from heatnrgx as `house_march`; its data is identical to `barbarastr_march` line by line, only the name differs. Done when the story is merged, `make check` is green, and the street's name appears nowhere in the fork's current tree outside `factory/`.

## Scope

- Replace `barbarastr_march` by `house_march` in the trace test, the integral test and the documents, and remove the old trace file.

## Out of scope

- The git history: the old name stays there; the owner accepted that.
- Every other trace, and the core.

## Stories

- S0001: Use the neutral trace name

## The human's changes

Before S0001: `tests/control/data/house_march.trace.gz`, exported from heatnrgx, added beside the old file. Done in commit "Add the March trace under a neutral name".

## Decisions taken for the human

1. The new trace's call count is the old one's (129,493): the data is identical.
2. `decisions.md` entries keep the old name if they have it; it is history.
