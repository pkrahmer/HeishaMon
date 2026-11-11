#!/usr/bin/env python3
"""A twin that breaks replies: it runs a program that serves the line protocol, the host twin,
passes every request to it and every reply back, and breaks the n-th reply after "ready", or
every reply, in one of the ways a serial line or a board can. It proves that replay.py restarts a
trace on each kind of break, gives up after its restarts, and prints what the link delivers
safely.

    garbling_twin.py [--at <n>] [--how <how>] [--byte <xx>] -- <program> [<argument>...]

--at <n>    the reply to break, counting from 1; every reply when not given
--how       frame: the first character replaced by --byte, the checksum kept (the default)
            error: a framed "error"
            fields: a framed "ok", the wrong number of fields for a call
            value: the first field replaced by "zz", which does not parse
            silent: no reply at all
--byte <xx> the replacing byte of "frame" in hexadecimal, "#" when not given
"""

import argparse
import subprocess
import sys
import threading

sys.dont_write_bytecode = True  # the checkout stays as it was

import replay  # noqa: E402  pylint: disable=wrong-import-position

HOWS = ("frame", "error", "fields", "value", "silent")


def parse_arguments(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--at", type=int, default=0, help="the reply to break, 0 for every one")
    parser.add_argument("--how", choices=HOWS, default="frame", help="how the reply breaks")
    parser.add_argument("--byte", default="23", help="the replacing byte in hexadecimal")
    parser.add_argument("program", nargs="+", help="the program that serves the protocol")
    return parser.parse_args(argv)


def broken(line, how, byte):
    """Returns the broken form of a reply line without its newline, or None for no reply."""
    if how == "frame":
        return byte + line[1:]
    if how == "error":
        return replay.frame("error")
    if how == "fields":
        return replay.frame("ok")
    if how == "value":
        fields = replay.strip_frame(line).split(" ")
        return replay.frame(" ".join(["zz"] + fields[1:]))
    return None


def forward(source, target):
    """Passes every request line on, then closes the program's input so that it ends too."""
    try:
        for line in source:
            target.write(line)
            target.flush()
        target.close()
    except BrokenPipeError:
        pass


def main(argv):
    arguments = parse_arguments(argv)
    byte = bytes.fromhex(arguments.byte)
    server = subprocess.Popen(arguments.program, stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    threading.Thread(target=forward, args=(sys.stdin.buffer, server.stdin), daemon=True).start()
    out = sys.stdout.buffer
    out.write(server.stdout.readline())
    out.flush()
    for count, line in enumerate(server.stdout, start=1):
        if arguments.at in (0, count):
            line = broken(line.rstrip(b"\n"), arguments.how, byte)
            if line is None:
                continue
            line += b"\n"
        out.write(line)
        out.flush()
    return server.wait()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
