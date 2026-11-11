# The contract between this fork and kumpel-factory is four targets: `check`, `lint`, `test`, `sync`.
# Upstream HeishaMon builds with arduino-cli (scripts/); this file covers only what runs on the
# host with gcc, g++ and python3, plus the device test sketch compiled for the D1 mini. The
# firmware build for the two boards is not part of `check` yet.
.PHONY: check lint test device device-replay sync facts-diff facts-files
.SILENT:

# The fork's own folders: the control code, its host tests, its documents.
CONTROL = HeishaMon/src/control
CORE = $(CONTROL)/core
TESTS = tests/control
# The device test: a sketch that serves the core over a serial line, and its host twin.
DEVICE = $(TESTS)/device
FQBN = esp8266:esp8266:d1_mini
# No fused multiply-add: the core must round as the Python reference does, twice where it does.
CFLAGS = -std=c99 -Wall -Wextra -Werror -pedantic -ffp-contract=off

SHELL := /bin/bash
.SHELLFLAGS := -eu -o pipefail -c
.ONESHELL:

# `$(start)` opens a recipe and `$(green)` ends it with "<target>: green (<seconds> s)",
# the line the factory's records of project commands read.
start = started=$$(date +%s%N)
green = tenths=$$(( ($$(date +%s%N) - started) / 100000000 )); echo "$@: green ($$((tenths / 10)).$$((tenths % 10)) s)"

check: lint test device
	echo "check: green"

# Nothing to fetch: the stage image (factory/stage.Containerfile) holds every tool.
sync:
	$(start)
	$(green)

# Two rules for the control code. Every .c file under it compiles on the host as strict C99
# without warnings; code that needs the Arduino framework is a .cpp file and is not checked here.
# The core includes nothing but the headers of its own folder and four headers of the standard
# library that do no input or output.
lint:
	$(start)
	if [ -d $(CONTROL) ]; then
	  while IFS= read -r source; do
	    gcc $(CFLAGS) -I$(CONTROL) -fsyntax-only "$$source"
	  done < <(find $(CONTROL) -name '*.c' | sort)
	fi
	if [ -d $(CORE) ]; then
	  foreign=$$(grep -rnE '^[[:space:]]*#[[:space:]]*include' $(CORE) \
	    | grep -vE '#[[:space:]]*include[[:space:]]*(<(math|stdbool|stdint|stddef)\.h>|"[A-Za-z0-9_]+\.h")[[:space:]]*$$' || true)
	  if [ -n "$$foreign" ]; then
	    echo "lint: the core may include only its own headers, math.h, stdbool.h, stdint.h and stddef.h:"
	    echo "$$foreign"
	    exit 1
	  fi
	fi
	$(green)

# Upstream's two host suites, then the control tests: every tests/control/test_*.c is one program,
# linked with every .c file of the control code and with the other .c files beside it (the shared
# helpers), and run from the root of the checkout. A program passes when it exits with 0.
# The golden traces are unpacked first, and CTL_TRACES names their folder.
test:
	$(start)
	Examples/Rules/run_tests.sh
	python3 tests/modbus/run_tests.py
	build=$$(mktemp -d); trap 'rm -rf "$$build"' EXIT
	mkdir -p "$$build/empty"
	control=$(CONTROL); [ -d "$$control" ] || control="$$build/empty"
	tests=$(TESTS); [ -d "$$tests" ] || tests="$$build/empty"
	sources=$$(find "$$control" -name '*.c' | sort)
	helpers=$$(find "$$tests" -maxdepth 1 -name '*.c' ! -name 'test_*.c' | sort)
	mkdir -p "$$build/traces"
	for trace in $$(find "$$tests" -path '*/data/*.trace.gz' | sort); do
	  gzip -dc "$$trace" > "$$build/traces/$$(basename "$$trace" .gz)"
	done
	export CTL_TRACES="$$build/traces"
	count=0
	for test in $$(find "$$tests" -maxdepth 1 -name 'test_*.c' | sort); do
	  program="$$build/$$(basename "$$test" .c)"
	  gcc $(CFLAGS) -I$(CONTROL) -I$(TESTS) "$$test" $$helpers $$sources -lm -o "$$program"
	  "$$program"
	  count=$$((count + 1))
	done
	echo "control: $$count test programs passed"
	# The device replay on the host: the server the board runs, built for the host, fed through a pipe.
	if [ -f $(DEVICE)/host_server.c ]; then
	  gcc $(CFLAGS) -I$(CONTROL) $(DEVICE)/host_server.c $$sources -lm -o "$$build/host_server"
	  python3 $(DEVICE)/replay.py --command "$$build/host_server" "$$CTL_TRACES/edge_cases.trace" "$$CTL_TRACES/flaky_transport.trace"
	fi
	# The device tools' own tests: each tests/control/device/test_*.py, run from the root, finds the
	# control code's sources in CTL_SOURCES and a scratch folder in CTL_BUILD.
	for script in $$(find $(DEVICE) -maxdepth 1 -name 'test_*.py' 2>/dev/null | sort); do
	  CTL_SOURCES="$$sources" CTL_BUILD="$$build" python3 "$$script"
	done
	$(green)

# Every device sketch (a folder under tests/control/device/ that holds a .ino of its own name),
# compiled for the D1 mini (ESP8266) with each folder of the control code as a library; nothing is
# flashed. Each sketch prints its name and its memory lines.
device:
	$(start)
	build=$$(mktemp -d); trap 'rm -rf "$$build"' EXIT
	libraries=$$(find $(CONTROL) -mindepth 1 -maxdepth 1 -type d | sort | sed 's/^/--library /')
	for sketch in $$(find $(DEVICE) -mindepth 1 -maxdepth 1 -type d 2>/dev/null | sort); do
	  name=$$(basename "$$sketch")
	  [ -f "$$sketch/$$name.ino" ] || continue
	  if ! arduino-cli compile --fqbn $(FQBN) $$libraries --build-path "$$build/$$name" --warnings all "$$sketch" > "$$build/$$name.log" 2>&1; then
	    cat "$$build/$$name.log"; exit 1
	  fi
	  echo "$$name:"
	  grep -E '^\. (Variables|Code in flash)' "$$build/$$name.log"
	done
	$(green)

# The same replay against the board on a serial port, run by hand on the machine the board is
# plugged into, with pyserial installed: make device-replay PORT=COM5. Never part of `check`.
PORT ?=
device-replay:
	if [ -z "$(PORT)" ]; then echo "device-replay: set PORT, as in make device-replay PORT=COM5"; exit 2; fi
	build=$$(mktemp -d); trap 'rm -rf "$$build"' EXIT
	for trace in $(TESTS)/data/*.trace.gz; do gzip -dc "$$trace" > "$$build/$$(basename "$$trace" .gz)"; done
	python3 $(DEVICE)/replay.py --port "$(PORT)" "$$build"/*.trace

# Start facts: the factory runs these before an agent and puts their output into its first
# message; never part of `check`. The base is where the branch left origin/develop.
base_ref = $$(git merge-base HEAD origin/develop)

# The branch's changes: the file list with counts, then the whole diff, without the factory's files.
facts-diff:
	base=$(base_ref); git diff --stat "$$base" -- . ':!factory'; echo; git diff "$$base" -- . ':!factory'

# The tracked files of the fork's own folders, and upstream's sources beside them.
facts-files:
	git ls-files -- $(CONTROL) $(TESTS) docs/control 'HeishaMon/*.h' 'HeishaMon/*.cpp' 'HeishaMon/*.ino'
