#!/usr/bin/env python3
"""The tests of the shadow's host twin and of shadow.py, run from the root of the checkout by
make test. It builds the twin with gcc from the sources in $CTL_SOURCES into a folder under
$CTL_BUILD, runs the bridge against it on the capture in data/, prints the bridge's summary line,
and prints one line per failed check with the file, the line and both values. Exits with 0 when
every check passed and 1 otherwise."""

import csv
import inspect
import json
import os
import shlex
import subprocess
import sys
import tempfile
import time

sys.dont_write_bytecode = True  # the checkout stays as it was

import replay  # noqa: E402  pylint: disable=wrong-import-position
import shadow  # noqa: E402  pylint: disable=wrong-import-position

DEVICE = os.path.dirname(os.path.abspath(__file__))
CONTROL = "HeishaMon/src/control"
SHADOW = os.path.join(DEVICE, "shadow.py")
EXAMPLE = os.path.join(DEVICE, "shadow.example.json")
CAPTURE = os.path.join(DEVICE, "data", "shadow_capture.jsonl")
RUN_LIMIT_S = 15.0

failures = 0


def check(actual, expected):
    """Passes when both are equal; prints the caller's file and line otherwise."""
    global failures  # pylint: disable=global-statement
    if actual == expected:
        return
    caller = inspect.stack()[1]
    print("%s:%d: got %r, expected %r" % (caller.filename, caller.lineno, actual, expected))
    failures += 1


def build_twin(folder):
    program = os.path.join(folder, "shadow_host")
    subprocess.run(["gcc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-pedantic",
                    "-ffp-contract=off", "-I" + CONTROL, os.path.join(DEVICE, "shadow_host.c")]
                   + os.environ["CTL_SOURCES"].split() + ["-lm", "-o", program], check=True)
    return program


def twin_lines(command, lines, open_s=0.0):
    """Starts the twin, writes the lines, keeps its input open for open_s and returns its output
    lines and its exit status."""
    process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE)
    process.stdin.write(b"".join(line + b"\n" for line in lines))
    process.stdin.flush()
    time.sleep(open_s)
    output, error = process.communicate(timeout=10)
    return output.splitlines(), process.returncode, error


def test_twin_starts(twin):
    lines, status, _ = twin_lines([twin], [replay.frame("G"), b"G *00"])
    check(lines, [replay.frame("ready"), replay.frame("ok"), replay.frame("error")])
    check(status, 0)


def test_twin_records(twin):
    lines, status, _ = twin_lines([twin, "--speed", "60"], [replay.frame("G")], open_s=2.5)
    texts = [replay.strip_frame(line) for line in lines]
    records = [text for text in texts if text is not None and text.startswith("D ")]
    check(len(records) >= 2, True)
    check(all(text.endswith(" heap=0") for text in records), True)
    check(status, 0)


def test_twin_stats(twin):
    lines, _, _ = twin_lines([twin], [replay.frame("S")])
    check(replay.strip_frame(lines[1]) if len(lines) > 1 else None,
          "stats inputs=0 records=0 heap=0 uptime_s=0")


def test_twin_usage(twin):
    for arguments in [["--speed", "0"], ["--speed", "-1"], ["--speed", "x"], ["--fast"]]:
        lines, status, error = twin_lines([twin] + arguments, [])
        check(lines, [])
        check(error, b"usage: shadow_host [--speed <factor>]\n")
        check(status, 2)


def run_bridge(arguments):
    return subprocess.run([sys.executable, SHADOW] + arguments, capture_output=True, text=True,
                          check=False, timeout=60)


def read_rows(path):
    with open(path, newline="", encoding="ascii") as file:
        return list(csv.DictReader(file))


def configured_messages():
    with open(EXAMPLE, encoding="utf-8") as file:
        topics = shadow.topic_inputs(json.load(file))
    with open(CAPTURE, encoding="utf-8") as file:
        messages = [json.loads(line) for line in file if line.strip()]
    check(any(message["topic"] not in topics for message in messages), True)
    return [message for message in messages if message["topic"] in topics]


def test_bridge(twin, folder):
    out = os.path.join(folder, "shadow_out")
    started = time.monotonic()
    result = run_bridge(["--config", EXAMPLE, "--command", shlex.quote(twin) + " --speed 120",
                         "--replay", CAPTURE, "--minutes", "0.1", "--out", out])
    check(time.monotonic() - started < RUN_LIMIT_S, True)
    check(result.returncode, 0)
    summary = [line for line in result.stdout.splitlines() if line.startswith("shadow:")]
    print("\n".join(summary))
    check(len(summary), 1)
    check(bool(summary) and summary[0].endswith(" 0 broken lines"), True)

    decisions = read_rows(os.path.join(out, "decisions.csv"))
    check(len(decisions) >= 10, True)
    reasons = [row["reason"] for row in decisions]
    check("SEEDED" in reasons, True)
    seeded = reasons.index("SEEDED") if "SEEDED" in reasons else len(reasons)
    check({row["rooms"] for row in decisions[seeded:]}, {"4:4.0/4.0"})
    check(all(row["wall_time"].endswith("Z") for row in decisions), True)

    inputs = read_rows(os.path.join(out, "inputs.csv"))
    expected = configured_messages()
    check([row["topic"] for row in inputs], [message["topic"] for message in expected])
    check({row["reply"] for row in inputs} <= {"taken", "no_reading"}, True)
    check([row["retained"] for row in inputs],
          ["yes" if message["retain"] else "no" for message in expected])
    check(os.path.getsize(os.path.join(out, "errors.log")), 0)


def write_config(folder, change):
    with open(EXAMPLE, encoding="utf-8") as file:
        config = json.load(file)
    change(config)
    path = os.path.join(folder, "shadow_config.json")
    with open(path, "w", encoding="utf-8") as file:
        json.dump(config, file)
    return path


def test_bridge_refuses(twin, folder):
    out = os.path.join(folder, "shadow_refused")
    without_rooms = write_config(folder, lambda config: config.pop("rooms"))
    result = run_bridge(["--config", without_rooms, "--command", twin, "--out", out,
                         "--replay", CAPTURE, "--minutes", "0.01"])
    check(result.returncode, 2)
    result = run_bridge(["--config", EXAMPLE, "--command", twin, "--port", "none", "--out", out,
                         "--replay", CAPTURE, "--minutes", "0.01"])
    check(result.returncode, 2)
    check(result.stdout, "")


def test_bridge_stops_on_a_bad_start(twin, folder):
    out = os.path.join(folder, "shadow_bad")
    zero_timeout = write_config(folder, lambda config: config.update(room_timeout_s=0))
    result = run_bridge(["--config", zero_timeout, "--command", twin, "--out", out,
                         "--replay", CAPTURE, "--minutes", "0.01"])
    check(result.returncode, 1)
    check(result.stdout, "")


class RecordingLink:
    def __init__(self):
        self.sent = []

    def send(self, texts):
        self.sent.extend(texts)


def test_forwarding(folder):
    output = shadow.Output(os.path.join(folder, "shadow_forward"))
    link = RecordingLink()
    bridge = shadow.Bridge(link, output, {"t/room": [("room", 2)], "t/heat": [("heat", 0)]})
    bridge.forward("t/room", b"21.5\r\n", True)
    bridge.forward("t/heat", "3200°".encode("utf-8"), False)
    bridge.forward("t/heat", b"1" * (shadow.LINE_MAX - len("I heat 0 ")), False)
    bridge.forward("t/heat", b"1" * (shadow.LINE_MAX - len("I heat 0 ") + 1), False)
    bridge.forward("t/other", b"1", False)
    check(link.sent, ["I room 2 21.5", "I heat 0 " + "1" * (shadow.LINE_MAX - len("I heat 0 "))])
    check(bridge.skipped, 2)
    bridge.receive(replay.frame("taken"))
    bridge.receive(replay.frame("D t=60.0 reason=NORMAL rooms=1:1.0/1.0"))
    bridge.receive(b"refused *00")
    bridge.receive(replay.frame("error"))
    bridge.receive(replay.frame("ok"))
    check(bridge.records, 1)
    check(bridge.broken, 1)
    check(bridge.counts, {"taken": 1, "no_reading": 0, "refused": 0, "bad": 0})
    check(bridge.summary(), "shadow: 1 records, 2 inputs (1 taken, 0 no reading, 0 refused, "
                            "0 bad), 2 skipped, 1 broken lines")
    output.close()
    with open(os.path.join(folder, "shadow_forward", "errors.log"), encoding="ascii") as file:
        check(len(file.read().splitlines()), 3)


def main():
    folder = tempfile.mkdtemp(dir=os.environ.get("CTL_BUILD"))
    twin = build_twin(folder)
    test_twin_starts(twin)
    test_twin_records(twin)
    test_twin_stats(twin)
    test_twin_usage(twin)
    test_bridge(twin, folder)
    test_bridge_refuses(twin, folder)
    test_bridge_stops_on_a_bad_start(twin, folder)
    test_forwarding(folder)
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
