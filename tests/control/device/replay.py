#!/usr/bin/env python3
"""Replays golden traces through the control core on a D1 mini, or through its host twin.

The core runs behind the line protocol of HeishaMon/src/control/wire/ctl_wire.h. This script reads
each trace (docs/control/golden-traces.md), sends its configuration and its calls, and compares
every reply with the values the Python reference recorded, exactly: the test exists to prove that
the board's compiler computes every bit as the host's does, so there is no tolerance.

    replay.py --command <program> <trace>...   the host twin, over its standard input and output
    replay.py --port <port> <trace>...         the board, over a serial port (needs pyserial)

Every line in both directions is framed: the text, " *" and the XOR of the text's bytes as two
lowercase hexadecimal digits. A broken line, an "error", a reply with the wrong fields or no reply
at all restarts the trace from its beginning, at most twice; a call is never sent again on its
own, because every call changes the core's state.

Exits with 0 when every trace passed, however often it restarted, 1 when one did not, 2 on a
wrong command line or when the other side never said "ready".
"""

import argparse
import collections
import math
import queue
import re
import shlex
import struct
import subprocess
import sys
import threading
import time

BAUD = 230400
READY_TIMEOUT_S = 10.0
REPLY_TIMEOUT_S = 10.0
DRAIN_S = 1.0
RESET_PULSE_S = 0.2
IN_FLIGHT = 4
RESTARTS_MAX = 2
MISMATCH_LIMIT = 10
# The trace runner's line buffer: a longer line is a format error there too.
TRACE_LINE_MAX = 1024

SIGNALS = ["T_INDOOR", "T_OUTDOOR", "P_PRODUCED", "P_SOLAR", "P_INTERNAL"]
REQUIRED_SIGNALS = {"T_INDOOR", "T_OUTDOOR", "P_PRODUCED"}
DEMANDS = ["HEATING", "COOLING", "NO_DEMAND"]
REASONS = ["NORMAL", "SEEDED", "WAITING_FOR_INPUTS", "MASKED", "HELD_STALE_INPUT",
           "FAULT_TIMEOUT", "FAULT_RECOVERED", "INVALID_TIME"]
PHASES = ["UNINITIALIZED", "RUNNING", "HOLD", "FAULT"]
STATE_KEYS = ["E", "int", "skip", "clamped"]
DECISION_KEYS = ["demand", "reason", "phase", "E", "dt", "int", "skip", "clamped", "pnet",
                 "unhealthy", "substituted", "ledger"]
LEDGER_KEYS = ["ledger.produced", "ledger.solar", "ledger.internal", "ledger.transmission",
               "ledger.trim"]
CONFIG_KEYS = {"desired_indoor_c", "transmission_w_per_k", "adjustment_w_per_k", "hysteresis_wh",
               "solar_gain_factor", "energy_clamp_factor", "max_step_s", "tick_interval_s",
               "integration_interval_s", "input_hold_s"}
# The board keeps no snapshot, so the core has no field for these two.
IGNORED_CONFIG_KEYS = {"state_max_age_s", "persist_interval_s"}
# Python's float() takes spellings that C's strtod does not, and the reverse; this is the part
# both read alike, which is all a trace holds.
NUMBER = re.compile(r"[-+]?(inf|infinity|nan|(\d+\.?\d*|\.\d+)(e[-+]?\d+)?)", re.IGNORECASE)
HEX_DOUBLE = re.compile(r"[0-9a-f]{16}")


class FormatError(Exception):
    """A trace line the format does not allow."""


class LinkError(Exception):
    """The other side closed the link."""


# ----------------------------------------------------------------------------------------------
# Lines on the link

FRAME = re.compile(rb"(.*) \*([0-9a-f]{2})", re.DOTALL)


def checksum(data):
    """The XOR of all bytes, as ctl_frame_checksum computes it."""
    total = 0
    for byte in data:
        total ^= byte
    return total


def frame(text):
    data = text.encode("ascii")
    return data + b" *%02x" % checksum(data)


def strip_frame(line):
    """Returns the text of a framed line, a trailing carriage return ignored, or None when the
    frame does not check or the text is not printable ASCII."""
    if line.endswith(b"\r"):
        line = line[:-1]
    match = FRAME.fullmatch(line)
    if match is None or checksum(match.group(1)) != int(match.group(2), 16):
        return None
    text = match.group(1)
    if any(byte < 0x20 or byte > 0x7e for byte in text):
        return None
    return text.decode("ascii")


def safe_text(line):
    """The bytes as printable ASCII, every other byte as \\xNN, so that no console can fail on
    what a broken link delivers."""
    return "".join(chr(byte) if 0x20 <= byte <= 0x7e else "\\x%02x" % byte for byte in line)


# ----------------------------------------------------------------------------------------------
# Values on the line

def to_hex(value):
    return struct.pack(">d", value).hex()


def from_hex(text):
    if not HEX_DOUBLE.fullmatch(text):
        raise ValueError(text)
    return struct.unpack(">d", bytes.fromhex(text))[0]


def same_number(expected, actual):
    """Equal by value, so -0.0 equals 0.0, or both NaN."""
    return expected == actual or (math.isnan(expected) and math.isnan(actual))


def signal_set_text(mask):
    names = [name for index, name in enumerate(SIGNALS) if mask & (1 << index)]
    return ",".join(names) if names else "-"


# ----------------------------------------------------------------------------------------------
# Reading a trace, as strictly as the trace runner reads it

def split(text, separator):
    fields = text.split(separator)
    if any(field == "" for field in fields):
        raise FormatError()
    return fields


def parse_number(text):
    if not NUMBER.fullmatch(text):
        raise FormatError()
    return float(text)


def parse_flag(text):
    if text not in ("0", "1"):
        raise FormatError()
    return int(text)


def parse_name(text, names):
    if text not in names:
        raise FormatError()
    return names.index(text)


def parse_set(text, names):
    if text == "-":
        return 0
    mask = 0
    for name in split(text, ","):
        mask |= 1 << parse_name(name, names)
    return mask


def pair_values(pairs, keys):
    if len(pairs) != len(keys):
        raise FormatError()
    values = []
    for pair, key in zip(pairs, keys):
        if not pair.startswith(key + "="):
            raise FormatError()
        values.append(pair[len(key) + 1:])
    return values


def parse_state(pairs):
    energy, integrated, skipped, clamped = pair_values(pairs, STATE_KEYS)
    return [("E", parse_number(energy)), ("int", parse_number(integrated)),
            ("skip", parse_number(skipped)), ("clamped", parse_flag(clamped))]


def parse_decision(pairs):
    values = pair_values(pairs, DECISION_KEYS)
    ledger = split(values[11], ",")
    if len(ledger) != len(LEDGER_KEYS):
        raise FormatError()
    expected = [("demand", parse_name(values[0], DEMANDS)),
                ("reason", parse_name(values[1], REASONS)),
                ("phase", parse_name(values[2], PHASES)),
                ("E", parse_number(values[3])), ("dt", parse_number(values[4])),
                ("int", parse_number(values[5])), ("skip", parse_number(values[6])),
                ("clamped", parse_flag(values[7])),
                ("pnet", None if values[8] == "none" else parse_number(values[8])),
                ("unhealthy", parse_set(values[9], SIGNALS)),
                ("substituted", parse_set(values[10], SIGNALS))]
    expected += [(key, parse_number(term)) for key, term in zip(LEDGER_KEYS, ledger)]
    return expected


def parse_request(name, arguments):
    """Returns the protocol's request for the trace's call."""
    if name == "observe" and len(arguments) == 3:
        t, signal, value = arguments
        return "O %s %d %s" % (to_hex(parse_number(t)), parse_name(signal, SIGNALS),
                               to_hex(parse_number(value)))
    if name == "advance" and len(arguments) == 1:
        return "A " + to_hex(parse_number(arguments[0]))
    if name == "allow" and len(arguments) == 2:
        return "M %s %x" % (to_hex(parse_number(arguments[0])), parse_set(arguments[1], DEMANDS))
    if name == "evaluate" and len(arguments) == 1:
        return "E " + to_hex(parse_number(arguments[0]))
    raise FormatError()


Call = collections.namedtuple("Call", "line request expected")


def parse_call(number, fields):
    if "|" not in fields:
        raise FormatError()
    bar = fields.index("|")
    request = parse_request(fields[0], fields[1:bar])
    pairs = fields[bar + 1:]
    expected = parse_decision(pairs) if request[0] == "E" else parse_state(pairs)
    return Call(number, request, expected)


def config_requests(key, text):
    if key == "trim_limit_k":
        if text == "none":
            return ["C trim_limit_enabled 0"]
        return ["C trim_limit_enabled 1", "C trim_limit_k " + to_hex(parse_number(text))]
    value = parse_number(text)
    if key in IGNORED_CONFIG_KEYS:
        return []
    for prefix, optional_only in (("max_age_s.", False), ("fallback_w.", True)):
        if key.startswith(prefix):
            signal = key[len(prefix):]
            if optional_only and signal in REQUIRED_SIGNALS:
                raise FormatError()
            return ["C %s%d %s" % (prefix, parse_name(signal, SIGNALS), to_hex(value))]
    if key not in CONFIG_KEYS:
        raise FormatError()
    return ["C %s %s" % (key, to_hex(value))]


def read_trace(path):
    """Returns the configuration requests and the calls; raises FormatError with the line."""
    config = []
    calls = []
    part = "version"
    number = 0
    with open(path, encoding="ascii", errors="replace", newline="\n") as file:
        for number, raw in enumerate(file, start=1):
            line = raw[:-1] if raw.endswith("\n") else raw
            try:
                if len(line) > TRACE_LINE_MAX:
                    raise FormatError()
                if part == "version":
                    if line != "heatnrgx-golden 1":
                        raise FormatError()
                    part = "header"
                    continue
                if line == "" or line.startswith("#"):
                    continue
                if part == "after end":
                    raise FormatError()
                fields = split(line, " ")
                if part == "header":
                    if fields[0] == "scenario" and len(fields) == 2:
                        pass
                    elif fields[0] == "config" and len(fields) == 3:
                        config += config_requests(fields[1], fields[2])
                    elif fields == ["begin"]:
                        part = "body"
                    else:
                        raise FormatError()
                elif fields[0] == "end":
                    if len(fields) != 2 or not fields[1].isdigit() or int(fields[1]) != len(calls):
                        raise FormatError()
                    part = "after end"
                else:
                    calls.append(parse_call(number, fields))
            except FormatError:
                raise FormatError(number)
    if part != "after end":
        raise FormatError(number + 1)
    return config, calls


# ----------------------------------------------------------------------------------------------
# The two links: a program's standard input and output, and a serial port. Each sends request
# texts framed, and receive() returns one raw line without its newline, or None when none came
# within the timeout.

class ProcessLink:
    def __init__(self, command):
        self.process = subprocess.Popen(shlex.split(command), stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE)
        self.lines = queue.Queue()
        # A thread reads the program's output, so that a reply that never comes is a timeout
        # and not a read that blocks forever, on every platform.
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()

    def _read(self):
        for line in self.process.stdout:
            self.lines.put(line)
        self.lines.put(None)

    def send(self, texts):
        self.process.stdin.write(b"".join(frame(text) + b"\n" for text in texts))
        self.process.stdin.flush()

    def receive(self, timeout):
        try:
            line = self.lines.get(timeout=timeout)
        except queue.Empty:
            return None
        if line is None:
            self.lines.put(None)
            raise LinkError("the program closed its output")
        return line.rstrip(b"\n")

    def close(self):
        try:
            self.process.stdin.close()
        except BrokenPipeError:
            pass
        self.process.wait()


class SerialLink:
    def __init__(self, port):
        import serial  # pylint: disable=import-outside-toplevel
        self.port = serial.Serial(port, BAUD, timeout=REPLY_TIMEOUT_S)
        self.reset()

    def reset(self):
        """Pulls the board's reset line, which RTS drives on a D1 mini, with DTR off so that it
        boots the sketch and not the loader; some ports do not reset the board when they open."""
        self.port.dtr = False
        self.port.rts = True
        time.sleep(RESET_PULSE_S)
        self.port.rts = False

    def send(self, texts):
        self.port.write(b"".join(frame(text) + b"\n" for text in texts))

    def receive(self, timeout):
        self.port.timeout = timeout
        line = self.port.readline()
        if not line.endswith(b"\n"):
            return None
        return line[:-1]

    def close(self):
        self.port.close()


def wait_ready(link):
    """Returns True when a framed "ready" came within READY_TIMEOUT_S. Every other line is
    ignored: the board prints its boot messages first, at another speed."""
    deadline = time.monotonic() + READY_TIMEOUT_S
    while True:
        left = deadline - time.monotonic()
        if left <= 0:
            return False
        try:
            line = link.receive(left)
        except LinkError:
            return False
        if line is not None and strip_frame(line) == "ready":
            return True


def drain(link):
    """Reads and drops replies for DRAIN_S, so that nothing still on the way is taken for the
    reply to a later request."""
    deadline = time.monotonic() + DRAIN_S
    while True:
        left = deadline - time.monotonic()
        if left <= 0 or link.receive(left) is None:
            return


# ----------------------------------------------------------------------------------------------
# Comparing a reply

class ReplyError(Exception):
    """A reply that is no answer to the call: "error", or the wrong fields."""


def decode_value(key, text):
    """Returns the reply's field as the trace records it: a number, an index or a mask."""
    if key in ("demand", "reason", "phase", "clamped"):
        if not text.isdigit():
            raise ValueError(text)
        return int(text)
    if key in ("unhealthy", "substituted"):
        if not re.fullmatch(r"[0-9a-f]{2}", text):
            raise ValueError(text)
        return int(text, 16)
    if key == "pnet" and text == "-":
        return None
    return from_hex(text)


def value_text(key, value):
    if value is None:
        return "none"
    if key == "demand":
        return DEMANDS[value] if value < len(DEMANDS) else str(value)
    if key == "reason":
        return REASONS[value] if value < len(REASONS) else str(value)
    if key == "phase":
        return PHASES[value] if value < len(PHASES) else str(value)
    if key in ("unhealthy", "substituted"):
        return signal_set_text(value)
    return repr(value)


def same_value(expected, actual):
    if expected is None or actual is None:
        return expected is actual
    if isinstance(expected, float):
        return same_number(expected, actual)
    return expected == actual


def compare(call, reply):
    """Returns the mismatches as (key, expected, actual); raises ReplyError."""
    fields = reply.split(" ")
    if len(fields) != len(call.expected):
        raise ReplyError()
    mismatches = []
    for (key, expected), text in zip(call.expected, fields):
        try:
            actual = decode_value(key, text)
        except ValueError:
            raise ReplyError() from None
        if not same_value(expected, actual):
            mismatches.append((key, value_text(key, expected), value_text(key, actual)))
    return mismatches


# ----------------------------------------------------------------------------------------------
# Replaying

class BrokenLine(Exception):
    """The link broke a line: the attempt stops, and the trace starts again. The arguments are
    the trace's line of the call, or None in the configuration, and what came instead."""


class Attempt:
    """What one attempt at a trace counted; a restart throws it away."""

    def __init__(self):
        self.calls = 0
        self.mismatches = 0
        self.failed = False


def failed_attempt():
    """An attempt that counted nothing and failed: a trace that could not be read or replayed."""
    attempt = Attempt()
    attempt.failed = True
    return attempt


def receive_text(link):
    """Returns the text of the next reply; raises BrokenLine without a line number when it is
    broken or does not come."""
    line = link.receive(REPLY_TIMEOUT_S)
    if line is None:
        raise BrokenLine(None, "no reply within %g s" % REPLY_TIMEOUT_S)
    text = strip_frame(line)
    if text is None:
        raise BrokenLine(None, safe_text(line))
    return text


def configure(link, path, config, attempt):
    """Sends R, the configuration and B one at a time; returns False when B found it bad."""
    for request in ["R"] + config + ["B"]:
        link.send([request])
        reply = receive_text(link)
        if reply == "error":
            raise BrokenLine(None, "%s -> error" % request)
        if reply != "ok":
            print("%s: %s: %s" % (path, request, reply))
            attempt.failed = True
            return False
    return True


def replay_calls(link, path, calls, attempt):
    """Keeps IN_FLIGHT requests on the way and compares each reply in order, until the end or
    MISMATCH_LIMIT mismatches."""
    pending = collections.deque()
    next_call = 0
    while next_call < len(calls) or pending:
        if next_call < len(calls) and len(pending) < IN_FLIGHT:
            batch = calls[next_call:next_call + IN_FLIGHT - len(pending)]
            link.send([call.request for call in batch])
            pending.extend(batch)
            next_call += len(batch)
            continue
        call = pending.popleft()
        try:
            reply = receive_text(link)
            mismatches = compare(call, reply)
        except BrokenLine as broken:
            raise BrokenLine(call.line, broken.args[1]) from None
        except ReplyError:
            raise BrokenLine(call.line, "%s -> %s" % (call.request, reply)) from None
        attempt.calls += 1
        for key, expected, actual in mismatches:
            print("%s:%d: %s: expected %s, got %s" % (path, call.line, key, expected, actual))
            attempt.mismatches += 1
        if attempt.mismatches >= MISMATCH_LIMIT:
            drain(link)
            return


def replay_attempt(link, path, config, calls):
    attempt = Attempt()
    if configure(link, path, config, attempt):
        replay_calls(link, path, calls, attempt)
    return attempt


def broken_place(path, broken):
    line = broken.args[0]
    return path if line is None else "%s:%d" % (path, line)


class FileResult:
    def __init__(self, attempt, restarts):
        self.calls = attempt.calls
        self.mismatches = attempt.mismatches
        self.failed = attempt.failed or attempt.mismatches > 0
        self.restarts = restarts


def replay_trace(link, path, config, calls):
    """Replays the trace, restarting it from R after a broken line, at most RESTARTS_MAX times;
    the result is the last attempt's."""
    restarts = 0
    while True:
        try:
            return FileResult(replay_attempt(link, path, config, calls), restarts)
        except BrokenLine as broken:
            place = broken_place(path, broken)
            if restarts == RESTARTS_MAX:
                print("%s: broken line: %s" % (place, broken.args[1]))
                print("%s: failed after %d restarts" % (path, RESTARTS_MAX))
                drain(link)
                return FileResult(failed_attempt(), restarts)
            restarts += 1
            print("%s: broken line, restarting (%d of %d): %s"
                  % (place, restarts, RESTARTS_MAX, broken.args[1]))
            drain(link)


def replay_file(link, path):
    try:
        config, calls = read_trace(path)
    except OSError as error:
        print("%s: cannot open: %s" % (path, error.strerror))
        result = FileResult(failed_attempt(), 0)
    except FormatError as error:
        print("%s:%d: format error" % (path, error.args[0]))
        result = FileResult(failed_attempt(), 0)
    else:
        result = replay_trace(link, path, config, calls)
    print("%s: %d calls, %d mismatches" % (path, result.calls, result.mismatches))
    return result


def print_stats(link):
    """Prints the stats line of the last trace; a broken one is printed safely and fails
    nothing, because it holds no result."""
    link.send(["S"])
    line = link.receive(REPLY_TIMEOUT_S)
    if line is None:
        print("device replay: no stats within %g s" % REPLY_TIMEOUT_S)
        return
    text = strip_frame(line)
    print(text if text is not None else "broken stats line: " + safe_text(line))


def parse_arguments(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    target = parser.add_mutually_exclusive_group(required=True)
    target.add_argument("--command", help="the host twin, a program and its arguments as a "
                                          "POSIX shell splits them")
    target.add_argument("--port", help="the serial port of the board")
    parser.add_argument("traces", nargs="+", help="golden trace files, unpacked")
    return parser.parse_args(argv)


def open_link(arguments):
    if arguments.command is not None:
        return ProcessLink(arguments.command)
    return SerialLink(arguments.port)


def main(argv):
    arguments = parse_arguments(argv)
    try:
        link = open_link(arguments)
    except Exception as error:  # pylint: disable=broad-except
        print("device replay: cannot open the link: %s" % error)
        return 2
    try:
        if not wait_ready(link):
            print("device replay: no ready")
            return 2
        results = [replay_file(link, path) for path in arguments.traces]
        print_stats(link)
    except LinkError as error:
        print("device replay: %s" % error)
        return 1
    finally:
        link.close()
    calls = sum(result.calls for result in results)
    mismatches = sum(result.mismatches for result in results)
    restarts = sum(result.restarts for result in results)
    print("device replay: %d files, %d calls, %d mismatches, %d restarts"
          % (len(results), calls, mismatches, restarts))
    return 0 if all(not result.failed for result in results) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
