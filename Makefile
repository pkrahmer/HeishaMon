# The contract between this fork and kumpel-factory is four targets: `check`, `lint`, `test`, `sync`.
# Upstream HeishaMon builds with arduino-cli (scripts/); this file covers what runs on the host with
# gcc and python3, and the firmware compiled for the two boards with arduino-cli.
.PHONY: check lint test firmware sync facts-diff facts-files
.SILENT:

# The fork's own folders: the control code, its host tests, its documents.
CONTROL = HeishaMon/src/control
CORE = $(CONTROL)/core
TESTS = tests/control
# The firmware's two boards, with the options of upstream's scripts/build_esp8266.sh and build_esp32s3.sh.
FIRMWARE_D1 = esp8266:esp8266:d1_mini:xtal=160,vt=flash,ssl=basic,mmu=3216,non32xfer=fast,eesz=4M2M,ip=lm2f,dbg=Disabled,lvl=None____,wipe=none,baud=921600
FIRMWARE_S3 = esp32:esp32:esp32s3:CDCOnBoot=cdc,PSRAM=enabled,PartitionScheme=min_spiffs
# No fused multiply-add: the core must round as the Python reference does, twice where it does.
CFLAGS = -std=c99 -Wall -Wextra -Werror -pedantic -ffp-contract=off

SHELL := /bin/bash
.SHELLFLAGS := -eu -o pipefail -c
.ONESHELL:

# `$(start)` opens a recipe and `$(green)` ends it with "<target>: green (<seconds> s)",
# the line the factory's records of project commands read.
start = started=$$(date +%s%N)
green = tenths=$$(( ($$(date +%s%N) - started) / 100000000 )); echo "$@: green ($$((tenths / 10)).$$((tenths % 10)) s)"

check: lint test firmware
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
	$(green)

# The firmware, HeishaMon/HeishaMon.ino with the control code under its src/, compiled for both boards;
# nothing is flashed. Each board prints its name and its memory lines.
firmware:
	$(start)
	build=$$(mktemp -d); trap 'rm -rf "$$build"' EXIT
	for target in $(FIRMWARE_D1) $(FIRMWARE_S3); do
	  name=$${target%%:*}
	  if ! arduino-cli compile --fqbn="$$target" --build-path "$$build/$$name" --warnings=none HeishaMon > "$$build/$$name.log" 2>&1; then
	    grep -E 'error|Error' "$$build/$$name.log" | head -40; exit 1
	  fi
	  echo "$$name:"
	  grep -E '^\. (Variables|Code in flash)|^Sketch uses|^Global variables' "$$build/$$name.log"
	done
	$(green)

# Start facts: the factory runs these before an agent and puts their output into its first
# message; never part of `check`. The base is where the branch left origin/develop.
base_ref = $$(git merge-base HEAD origin/develop)

# The branch's changes: the file list with counts, then the whole diff, without the factory's files.
facts-diff:
	base=$(base_ref); git diff --stat "$$base" -- . ':!factory'; echo; git diff "$$base" -- . ':!factory'

# The tracked files of the fork's own folders, and upstream's sources beside them.
facts-files:
	git ls-files -- $(CONTROL) $(TESTS) docs/control 'HeishaMon/*.h' 'HeishaMon/*.cpp' 'HeishaMon/*.ino'
