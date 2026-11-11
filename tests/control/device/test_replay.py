#!/usr/bin/env python3
"""The tests of the host twin's frames and of replay.py's restarts, reset and start, run from the
root of the checkout by make test. It builds the twin with gcc from the sources in $CTL_SOURCES
into a folder under $CTL_BUILD, takes the traces from $CTL_TRACES or unpacks them there, and
prints one line per failed check with the file, the line and both values. Exits with 0 when
every check passed and 1 otherwise."""

import contextlib
import gzip
import inspect
import io
import os
import shlex
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True  # the checkout stays as it was

import replay  # noqa: E402  pylint: disable=wrong-import-position

DEVICE = os.path.dirname(os.path.abspath(__file__))
CONTROL = "HeishaMon/src/control"
REPLAY = os.path.join(DEVICE, "replay.py")
GARBLING_TWIN = os.path.join(DEVICE, "garbling_twin.py")
EDGE_CASES = "edge_cases.trace"
FLAKY_TRANSPORT = "flaky_transport.trace"
EDGE_CASES_CALLS = 3236
GARBLED_REPLY = 500

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
    program = os.path.join(folder, "host_server")
    subprocess.run(["gcc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-pedantic",
                    "-ffp-contract=off", "-I" + CONTROL, os.path.join(DEVICE, "host_server.c")]
                   + os.environ["CTL_SOURCES"].split() + ["-lm", "-o", program], check=True)
    return program


def trace_path(folder, name):
    traces = os.environ.get("CTL_TRACES")
    if traces:
        return os.path.join(traces, name)
    path = os.path.join(folder, name)
    with gzip.open("tests/control/data/" + name + ".gz") as packed, open(path, "wb") as out:
        out.write(packed.read())
    return path


def call_of_reply(trace, reply):
    """Returns the trace's call whose reply is the reply-th after "ready", in a replay without
    restarts: R, the configuration and B come first."""
    config, calls = replay.read_trace(trace)
    return calls[reply - len(config) - 3]


def twin_lines(command, requests):
    """Pipes the request lines to the program and returns its output lines and exit status."""
    done = subprocess.run(command, input=b"".join(line + b"\n" for line in requests),
                          capture_output=True, timeout=60, check=False)
    return done.stdout.splitlines(), done.returncode, done.stderr


def run_replay(command, *traces):
    done = subprocess.run([sys.executable, "-B", REPLAY, "--command", command] + list(traces),
                          capture_output=True, timeout=120, check=False)
    return done.stdout.decode("utf-8", errors="replace").splitlines(), done.returncode


def test_twin_frames(twin):
    lines, status, _ = twin_lines([twin], [b"R *52", b"R *53"])
    check(lines, [replay.frame("ready"), b"ok *04", replay.frame("error")])
    check(status, 0)


def test_twin_garbles(twin):
    lines, _, _ = twin_lines([twin, "--garble", "3"], [b"R *52"] * 5)
    check(len(lines), 6)
    check(replay.strip_frame(lines[0]), "ready")
    check([replay.strip_frame(line) for line in lines[1:]], ["ok", "ok", None, "ok", "ok"])
    check(lines[3], b"#k *04")


def test_twin_usage(twin):
    for arguments in (["--garble", "x"], ["--garble"], ["--garble", "-1"],
                      ["--garble", "1234567890"], ["--other", "1"]):
        lines, status, error = twin_lines([twin] + arguments, [b"R *52"])
        check(lines, [])
        check(status, 2)
        check(error, b"usage: host_server [--garble <n>]\n")


def test_twin_garbles_nothing(twin):
    for arguments in ([], ["--garble", "0"]):
        lines, _, _ = twin_lines([twin] + arguments, [b"R *52"] * 3)
        check([replay.strip_frame(line) for line in lines], ["ready", "ok", "ok", "ok"])


def garbling_command(twin, *options):
    return " ".join(shlex.quote(word) for word in
                    [sys.executable, GARBLING_TWIN] + list(options) + ["--", twin])


def restart_lines(lines):
    return [line for line in lines if "broken line, restarting" in line]


def test_restart(twin, trace):
    lines, status = run_replay("%s --garble %d" % (shlex.quote(twin), GARBLED_REPLY), trace)
    call = call_of_reply(trace, GARBLED_REPLY)
    restarts = restart_lines(lines)
    check(len(restarts), 1)
    prefix = "%s:%d: broken line, restarting (1 of 2): #" % (trace, call.line)
    check(restarts[0].startswith(prefix) if restarts else None, True)
    check(lines[-1] if lines else None,
          "device replay: 1 files, %d calls, 0 mismatches, 1 restarts" % EDGE_CASES_CALLS)
    check(status, 0)


def test_restart_after_answer(twin, trace):
    """An "error", the wrong number of fields and a field that does not parse each restart."""
    call = call_of_reply(trace, GARBLED_REPLY)
    place = "%s:%d: broken line, restarting (1 of 2): " % (trace, call.line)
    for how, what in (("error", "error"), ("fields", "ok"), ("value", "zz ")):
        command = garbling_command(twin, "--at", str(GARBLED_REPLY), "--how", how)
        lines, status = run_replay(command, trace)
        restarts = restart_lines(lines)
        check(len(restarts), 1)
        prefix = place + "%s -> %s" % (call.request, what)
        check(restarts[0].startswith(prefix) if restarts else None, True)
        check(lines[-1] if lines else None,
              "device replay: 1 files, %d calls, 0 mismatches, 1 restarts" % EDGE_CASES_CALLS)
        check(status, 0)


def test_restart_after_silence(twin, trace):
    """No reply restarts too; the timeout is shortened here so that the test does not wait."""
    command = garbling_command(twin, "--at", "1", "--how", "silent")
    timeout = replay.REPLY_TIMEOUT_S
    replay.REPLY_TIMEOUT_S = 0.5
    output = io.StringIO()
    try:
        with contextlib.redirect_stdout(output):
            status = replay.main(["--command", command, trace])
    finally:
        replay.REPLY_TIMEOUT_S = timeout
    lines = output.getvalue().splitlines()
    check(restart_lines(lines),
          ["%s: broken line, restarting (1 of 2): no reply within 0.5 s" % trace])
    check(lines[-1] if lines else None,
          "device replay: 1 files, %d calls, 0 mismatches, 1 restarts" % EDGE_CASES_CALLS)
    check(status, 0)


def test_no_restart(twin, edge_cases, flaky_transport):
    lines, status = run_replay(shlex.quote(twin), edge_cases, flaky_transport)
    check(lines[-1] if lines else None,
          "device replay: 2 files, 10639 calls, 0 mismatches, 0 restarts")
    check(status, 0)


def test_restart_limit(twin, trace):
    lines, status = run_replay(garbling_command(twin), trace)
    check(len(restart_lines(lines)), 2)
    check("%s: broken line: #k *04" % trace in lines, True)
    check("%s: failed after 2 restarts" % trace in lines, True)
    check(lines[-1] if lines else None, "device replay: 1 files, 0 calls, 0 mismatches, 2 restarts")
    check(status, 1)


def test_safe_printing(twin, trace):
    command = garbling_command(twin, "--byte", "e9")
    done = subprocess.run([sys.executable, "-B", REPLAY, "--command", command, trace],
                          capture_output=True, timeout=120, check=False)
    check(done.stderr, b"")
    check(all(byte < 0x80 for byte in done.stdout), True)
    restarts = [line for line in done.stdout.splitlines() if b"broken line, restarting" in line]
    check(len(restarts), 2)
    check(all(line.endswith(b": \\xe9k *04") for line in restarts), True)
    check(done.returncode, 1)


class RecordingPort:
    """A serial port that records what is done to its lines."""

    def __init__(self, events):
        self.__dict__["events"] = events

    def __setattr__(self, name, value):
        self.events.append((name, value))


class RecordingClock:
    def __init__(self, events):
        self.events = events

    def sleep(self, seconds):
        self.events.append(("sleep", seconds))


def test_reset():
    events = []
    link = replay.SerialLink.__new__(replay.SerialLink)
    link.port = RecordingPort(events)
    clock = replay.time
    replay.time = RecordingClock(events)
    try:
        link.reset()
    finally:
        replay.time = clock
    check(events, [("dtr", False), ("rts", True), ("sleep", 0.2), ("rts", False)])


class ScriptedLink:
    """A link that delivers the given lines, then nothing; it records what was sent."""

    def __init__(self, lines):
        self.lines = list(lines)
        self.sent = []

    def send(self, texts):
        self.sent += texts

    def receive(self, timeout):  # pylint: disable=unused-argument
        return self.lines.pop(0) if self.lines else None


def test_wait_ready():
    link = ScriptedLink([b"\xe9\x00boot", b"ready", b"ready *00", b"ready *6b", b"ok *04"])
    check(replay.wait_ready(link), True)
    check(link.lines, [b"ok *04"])
    timeout = replay.READY_TIMEOUT_S
    replay.READY_TIMEOUT_S = 0.1
    try:
        check(replay.wait_ready(ScriptedLink([b"ready", b"ready *6B"])), False)
    finally:
        replay.READY_TIMEOUT_S = timeout


def configure(replies, config):
    """Returns what configure did with the replies: True, False, or the break's text."""
    link = ScriptedLink(replies)
    attempt = replay.Attempt()
    output = io.StringIO()
    try:
        with contextlib.redirect_stdout(output):
            return replay.configure(link, "t", config, attempt)
    except replay.BrokenLine as broken:
        return broken.args
    finally:
        check(attempt.failed, output.getvalue() != "")


def test_configure():
    ok = replay.frame("ok")
    check(configure([ok, ok, ok], ["C x 1"]), True)
    check(configure([ok, replay.frame("bad 00000001")], []), False)
    stale = replay.frame("0000000000000000 0000000000000000 0000000000000000 0")
    check(configure([stale], []), (None, "R -> 0000000000000000 0000000000000000 "
                                         "0000000000000000 0"))
    check(configure([ok, replay.frame("bad 00000001")], ["C x 1"]), (None, "C x 1 -> bad 00000001"))
    check(configure([replay.frame("error")], []), (None, "R -> error"))


def test_frame_functions():
    check(replay.frame("ok"), b"ok *04")
    check(replay.strip_frame(b"ok *04\r"), "ok")
    check(replay.strip_frame(b"ok *0A"), None)
    check(replay.strip_frame(b"\xe9k *04"), None)
    check(replay.safe_text(b"\xe9k\t *04"), "\\xe9k\\x09 *04")


def main():
    if "CTL_SOURCES" not in os.environ or "CTL_BUILD" not in os.environ:
        print("%s: CTL_SOURCES and CTL_BUILD are not set; make test sets them" % __file__)
        return 1
    test_frame_functions()
    test_reset()
    test_wait_ready()
    test_configure()
    folder = tempfile.mkdtemp(prefix="test_replay.", dir=os.environ["CTL_BUILD"])
    twin = build_twin(folder)
    edge_cases = trace_path(folder, EDGE_CASES)
    flaky_transport = trace_path(folder, FLAKY_TRANSPORT)
    test_twin_frames(twin)
    test_twin_garbles(twin)
    test_twin_usage(twin)
    test_twin_garbles_nothing(twin)
    test_no_restart(twin, edge_cases, flaky_transport)
    test_restart(twin, edge_cases)
    test_restart_after_answer(twin, edge_cases)
    test_restart_after_silence(twin, edge_cases)
    test_restart_limit(twin, edge_cases)
    test_safe_printing(twin, edge_cases)
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
