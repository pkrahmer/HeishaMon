# The factory's folder

What kumpel-factory (github.com/pkrahmer/kumpel-factory) reads to build this fork's own code. Upstream HeishaMon has no such folder; nothing in it is compiled into the firmware.

- `pipeline.yml` The pipeline definition: a builder, a reviewer and the human's approval.
- `agents/` The two agent files of this project.
- `stage.Containerfile` The tools `make check` needs on top of the factory's stage image.
- `story-form.md`, `feature-form.md` The forms a story and a feature follow.
- `features/` One folder per feature, with its stories.
