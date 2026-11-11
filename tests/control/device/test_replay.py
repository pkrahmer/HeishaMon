#!/usr/bin/env python3
"""The tests of the host twin's frames and of replay.py's restarts, run from the root of the
checkout. It builds the twin with gcc, takes edge_cases.trace from $CTL_TRACES or unpacks it,
and prints one line per failed check with the file, the line and both values. Exits with 0
when every check passed and 1 otherwise."""

import gzip
import inspect
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
EDGE_CASES_CALLS = 3236

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
    sources = sorted(os.path.join(root, name) for root, _, names in os.walk(CONTROL)
                     for name in names if name.endswith(".c"))
    program = os.path.join(folder, "host_server")
    subprocess.run(["gcc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-pedantic",
                    "-ffp-contract=off", "-I" + CONTROL, os.path.join(DEVICE, "host_server.c")]
                   + sources + ["-lm", "-o", program], check=True)
    return program


def edge_cases_trace(folder):
    traces = os.environ.get("CTL_TRACES")
    if traces:
        return os.path.join(traces, EDGE_CASES)
    path = os.path.join(folder, EDGE_CASES)
    with gzip.open("tests/control/data/" + EDGE_CASES + ".gz") as packed, open(path, "wb") as out:
        out.write(packed.read())
    return path


def twin_lines(command, requests):
    """Pipes the request lines to the program and returns its output lines and exit status."""
    done = subprocess.run(command, input=b"".join(line + b"\n" for line in requests),
                          capture_output=True, timeout=60, check=False)
    return done.stdout.splitlines(), done.returncode, done.stderr


def run_replay(command, trace):
    done = subprocess.run([sys.executable, "-B", REPLAY, "--command", command, trace],
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
    for arguments in (["--garble", "x"], ["--garble"], ["--garble", "-1"], ["--other", "1"]):
        lines, status, error = twin_lines([twin] + arguments, [b"R *52"])
        check(lines, [])
        check(status, 2)
        check(error, b"usage: host_server [--garble <n>]\n")


def test_twin_garbles_nothing(twin):
    for arguments in ([], ["--garble", "0"]):
        lines, _, _ = twin_lines([twin] + arguments, [b"R *52"] * 3)
        check([replay.strip_frame(line) for line in lines], ["ready", "ok", "ok", "ok"])


def test_restart(twin, trace):
    lines, status = run_replay("%s --garble 500" % shlex.quote(twin), trace)
    restarts = [line for line in lines if "broken line, restarting" in line]
    check(len(restarts), 1)
    check(restarts[0].startswith(trace + ":") if restarts else False, True)
    check(lines[-1] if lines else None,
          "device replay: 1 files, %d calls, 0 mismatches, 1 restarts" % EDGE_CASES_CALLS)
    check(status, 0)


def test_restart_limit(trace):
    command = "%s %s" % (shlex.quote(sys.executable), shlex.quote(GARBLING_TWIN))
    lines, status = run_replay(command, trace)
    check(sum("broken line, restarting" in line for line in lines), 2)
    check("%s: failed after 2 restarts" % trace in lines, True)
    check(lines[-1] if lines else None, "device replay: 1 files, 0 calls, 0 mismatches, 2 restarts")
    check(status, 1)


def test_safe_printing(trace):
    command = "%s %s --byte e9" % (shlex.quote(sys.executable), shlex.quote(GARBLING_TWIN))
    done = subprocess.run([sys.executable, "-B", REPLAY, "--command", command, trace],
                          capture_output=True, timeout=120, check=False)
    check(done.stderr, b"")
    check(all(byte < 0x80 for byte in done.stdout), True)
    restarts = [line for line in done.stdout.splitlines() if b"broken line, restarting" in line]
    check(len(restarts), 2)
    check(all(line.endswith(b": \\xe9k *04") for line in restarts), True)
    check(done.returncode, 1)


def test_frame_functions():
    check(replay.frame("ok"), b"ok *04")
    check(replay.strip_frame(b"ok *04\r"), "ok")
    check(replay.strip_frame(b"ok *0A"), None)
    check(replay.strip_frame(b"\xe9k *04"), None)
    check(replay.safe_text(b"\xe9k\t *04"), "\\xe9k\\x09 *04")


def main():
    test_frame_functions()
    with tempfile.TemporaryDirectory() as folder:
        twin = build_twin(folder)
        trace = edge_cases_trace(folder)
        test_twin_frames(twin)
        test_twin_garbles(twin)
        test_twin_usage(twin)
        test_twin_garbles_nothing(twin)
        test_restart(twin, trace)
        test_restart_limit(trace)
        test_safe_printing(trace)
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
