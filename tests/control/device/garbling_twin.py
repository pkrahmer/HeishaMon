#!/usr/bin/env python3
"""A twin whose every reply is broken: it says a framed "ready", then answers every request line
with "ok" whose first character is replaced by one byte, the checksum of "ok" kept, so that no
reply's frame checks. It proves that replay.py gives up on a trace after its restarts, and that
it prints what a broken link delivers safely.

    garbling_twin.py [--byte <two hex digits>]   the byte that replaces, "#" when not given
"""

import argparse
import sys


def framed(text):
    total = 0
    for byte in text:
        total ^= byte
    return text + b" *%02x" % total


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--byte", default="23", help="the replacing byte in hexadecimal")
    garble = bytes.fromhex(parser.parse_args(argv).byte)
    out = sys.stdout.buffer
    out.write(framed(b"ready") + b"\n")
    out.flush()
    for _ in sys.stdin.buffer:
        out.write(garble + framed(b"ok")[1:] + b"\n")
        out.flush()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
