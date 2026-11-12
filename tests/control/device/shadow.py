#!/usr/bin/env python3
"""Feeds the shadow sketch, or its host twin, live from the house and writes what it decides.

The board runs the control loop behind the shadow protocol of
HeishaMon/src/control/loop/ctl_shadow.h. This script configures and starts it, subscribes
read-only to the owner's MQTT broker, forwards each message on a configured topic as an input
line, and appends every decision record and every input to CSV files with the wall time.

    shadow.py --config <file> --command <program> --out <folder> [--minutes <m>] [--replay <capture>]
    shadow.py --config <file> --port <port> --out <folder> [--minutes <m>]

With --replay it reads a capture file of JSON lines instead of the broker. Without --minutes it
runs until interrupted. The link and its frames are those of replay.py.

Exits with 0 after a run, 1 when the board does not start or the link breaks, 2 on a wrong
command line or configuration.
"""

import argparse
import csv
import datetime
import json
import math
import os
import queue
import sys
import threading
import time

sys.dont_write_bytecode = True  # the checkout stays as it was

import replay  # noqa: E402  pylint: disable=wrong-import-position

# The protocol's longest request line, without frame and line end (CTL_SHADOW_LINE_MAX).
LINE_MAX = 240
ROOMS_MAX = 16
START_REPLY_TIMEOUT_S = 10.0
# The longest wait for an event; an interrupt and the end of the run are seen this often.
WAIT_S = 0.1
# How long the end of a run waits for the replies still on the way.
DRAIN_S = 1.0
INPUT_REPLIES = ["taken", "no_reading", "refused", "bad"]
PUMP_INPUTS = ["outdoor", "heat", "valve", "defrost", "pv"]
OPTIONAL_INPUTS = {"defrost", "pv"}
CONFIG_KEYS = {"broker", "rooms", "outdoor", "heat", "valve", "defrost", "pv", "room_timeout_s",
               "solar_gain_factor", "cooling"}
INPUT_COLUMNS = ["wall_time", "input", "room", "topic", "payload", "retained", "reply"]


class ConfigError(Exception):
    """A configuration the bridge cannot run with."""


# ----------------------------------------------------------------------------------------------
# Configuration

def is_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) \
        and math.isfinite(value)


def require_topic(value, where):
    if not isinstance(value, str) or value == "":
        raise ConfigError("%s must be a topic" % where)
    return value


def require_number(config, key):
    if not is_number(config.get(key)):
        raise ConfigError("%s must be a number" % key)


def check_broker(broker):
    if not isinstance(broker, dict) or not isinstance(broker.get("host"), str) \
            or not isinstance(broker.get("port"), int) or isinstance(broker.get("port"), bool) \
            or not 0 < broker["port"] < 65536:
        raise ConfigError("broker must hold a host and a port")


def check_rooms(rooms):
    if not isinstance(rooms, list) or not 1 <= len(rooms) <= ROOMS_MAX:
        raise ConfigError("rooms must be a list of 1 to %d rooms" % ROOMS_MAX)
    for index, room in enumerate(rooms):
        if not isinstance(room, dict) or set(room) != {"topic", "weight"}:
            raise ConfigError("room %d must hold a topic and a weight" % index)
        require_topic(room["topic"], "the topic of room %d" % index)
        if not is_number(room["weight"]):
            raise ConfigError("the weight of room %d must be a number" % index)


def check_config(config):
    """Raises ConfigError for anything the bridge cannot run with; the board checks the values'
    ranges itself and answers the start with "bad"."""
    if not isinstance(config, dict):
        raise ConfigError("the configuration must be an object")
    missing = CONFIG_KEYS - set(config)
    unknown = set(config) - CONFIG_KEYS
    if missing or unknown:
        raise ConfigError("missing keys %s, unknown keys %s" % (sorted(missing), sorted(unknown)))
    check_broker(config["broker"])
    check_rooms(config["rooms"])
    for name in PUMP_INPUTS:
        if name not in OPTIONAL_INPUTS or config[name] is not None:
            require_topic(config[name], name)
    require_number(config, "room_timeout_s")
    require_number(config, "solar_gain_factor")
    if not isinstance(config["cooling"], bool):
        raise ConfigError("cooling must be true or false")


def read_config(path):
    try:
        with open(path, encoding="utf-8") as file:
            config = json.load(file)
    except (OSError, ValueError) as error:
        raise ConfigError(str(error)) from error
    check_config(config)
    return config


def number_text(value):
    """The shortest text that reads back as the same double; the board parses it as a bare
    number."""
    return repr(float(value))


def start_requests(config):
    requests = ["K rooms %d" % len(config["rooms"])]
    for index, room in enumerate(config["rooms"]):
        requests.append("K weight %d %s" % (index, number_text(room["weight"])))
    requests.append("K room_timeout_s " + number_text(config["room_timeout_s"]))
    requests.append("K solar_gain_factor " + number_text(config["solar_gain_factor"]))
    requests.append("K cooling %d" % (1 if config["cooling"] else 0))
    requests.append("G")
    return requests


def topic_inputs(config):
    """Maps each configured topic to the inputs it feeds, as (input, room) pairs."""
    inputs = {}
    for index, room in enumerate(config["rooms"]):
        inputs.setdefault(room["topic"], []).append(("room", index))
    for name in PUMP_INPUTS:
        if config[name] is not None:
            inputs.setdefault(config[name], []).append((name, 0))
    return inputs


# ----------------------------------------------------------------------------------------------
# Sources of messages: the broker, or a capture. Each puts ("message", topic, payload, retained)
# on the events queue from its own thread.

class Capture:
    def __init__(self, path, events):
        self.messages = []
        with open(path, encoding="utf-8") as file:
            for line in file:
                if line.strip():
                    item = json.loads(line)
                    self.messages.append((float(item["after_s"]), item["topic"],
                                          item["payload"].encode("utf-8"), bool(item["retain"])))
        self.messages.sort(key=lambda message: message[0])
        self.events = events
        self.stopped = threading.Event()

    def start(self, topics):
        del topics  # the bridge drops the messages on other topics itself
        threading.Thread(target=self._deliver, daemon=True).start()

    def _deliver(self):
        started = time.monotonic()
        for after_s, topic, payload, retained in self.messages:
            if self.stopped.wait(max(0.0, started + after_s - time.monotonic())):
                return
            self.events.put(("message", topic, payload, retained))

    def stop(self):
        self.stopped.set()


class Broker:
    def __init__(self, broker, events):
        import paho.mqtt.client as mqtt  # pylint: disable=import-outside-toplevel
        try:
            self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
        except AttributeError:  # paho-mqtt before 2.0
            self.client = mqtt.Client()
        self.host = broker["host"]
        self.port = broker["port"]
        self.events = events
        self.topics = []
        self.client.on_connect = self._connected
        self.client.on_message = self._received

    def start(self, topics):
        self.topics = sorted(topics)
        self.client.connect_async(self.host, self.port)
        self.client.loop_start()

    def _connected(self, client, *_):
        # Subscribing on every connect brings the subscriptions back after a reconnect.
        for topic in self.topics:
            client.subscribe(topic, qos=0)

    def _received(self, client, userdata, message):
        del client, userdata
        self.events.put(("message", message.topic, message.payload, bool(message.retain)))

    def stop(self):
        self.client.loop_stop()
        self.client.disconnect()


# ----------------------------------------------------------------------------------------------
# Output

def wall_time():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%fZ")


class CsvFile:
    """A CSV file appended to; its header is written when the file is new or empty."""

    def __init__(self, path):
        self.file = open(path, "a", newline="", encoding="ascii")  # pylint: disable=consider-using-with
        self.writer = csv.writer(self.file)
        self.needs_header = self.file.tell() == 0

    def write(self, header, row):
        if self.needs_header:
            self.writer.writerow(header)
            self.needs_header = False
        self.writer.writerow(row)
        self.file.flush()

    def close(self):
        self.file.close()


class Output:
    def __init__(self, folder):
        os.makedirs(folder, exist_ok=True)
        self.decisions = CsvFile(os.path.join(folder, "decisions.csv"))
        self.inputs = CsvFile(os.path.join(folder, "inputs.csv"))
        self.errors = open(os.path.join(folder, "errors.log"), "a",  # pylint: disable=consider-using-with
                           encoding="ascii")

    def decision(self, text):
        pairs = [field.split("=", 1) for field in text.split(" ")]
        self.decisions.write(["wall_time"] + [pair[0] for pair in pairs],
                             [wall_time()] + [pair[1] if len(pair) > 1 else "" for pair in pairs])

    def input(self, item, reply):
        self.inputs.write(INPUT_COLUMNS, [item.wall_time, item.input, item.room, item.topic,
                                          item.payload, "yes" if item.retained else "no", reply])

    def error(self, text):
        self.errors.write("%s %s\n" % (wall_time(), text))
        self.errors.flush()

    def close(self):
        self.decisions.close()
        self.inputs.close()
        self.errors.close()


# ----------------------------------------------------------------------------------------------
# The run

class InputItem:
    def __init__(self, topic, payload, retained, input_name, room):
        self.wall_time = wall_time()
        self.topic = topic
        self.payload = payload
        self.retained = retained
        self.input = input_name
        self.room = room


def payload_text(payload):
    """The payload as one line of printable ASCII without its line ends, or None."""
    try:
        text = payload.decode("ascii")
    except UnicodeDecodeError:
        return None
    text = text.replace("\r", "").replace("\n", "")
    if any(not 0x20 <= ord(char) <= 0x7e for char in text):
        return None
    return text


class Bridge:
    def __init__(self, link, output, inputs):
        self.link = link
        self.output = output
        self.inputs = inputs
        self.pending = []
        self.counts = dict.fromkeys(INPUT_REPLIES, 0)
        self.records = 0
        self.sent = 0
        self.skipped = 0
        self.broken = 0

    def forward(self, topic, payload, retained):
        text = payload_text(payload)
        for input_name, room in self.inputs.get(topic, []):
            if text is None:
                self.skip(InputItem(topic, replay.safe_text(payload), retained, input_name, room))
                continue
            item = InputItem(topic, text, retained, input_name, room)
            request = "I %s %d %s" % (input_name, room, text)
            if len(request) > LINE_MAX:
                self.skip(item)
                continue
            self.link.send([request])
            self.pending.append(item)
            self.sent += 1

    def skip(self, item):
        self.skipped += 1
        self.output.input(item, "skipped")

    def receive(self, line):
        text = replay.strip_frame(line)
        if text is None:
            self.broken += 1
            self.output.error("broken line: " + replay.safe_text(line))
        elif text.startswith("D "):
            self.records += 1
            self.output.decision(text[2:])
        elif self.pending:
            item = self.pending.pop(0)
            self.output.input(item, text)
            if text in self.counts:
                self.counts[text] += 1
            else:
                self.output.error("unexpected reply to %s %d: %s" % (item.input, item.room, text))
        else:
            self.output.error("unexpected line: " + text)

    def finish(self):
        for item in self.pending:
            self.output.input(item, "-")
            self.output.error("no reply to %s %d" % (item.input, item.room))
        self.pending = []

    def summary(self):
        return ("shadow: %d records, %d inputs (%d taken, %d no reading, %d refused, %d bad), "
                "%d skipped, %d broken lines"
                % (self.records, self.sent, self.counts["taken"], self.counts["no_reading"],
                   self.counts["refused"], self.counts["bad"], self.skipped, self.broken))


class SerialLink(replay.SerialLink):
    """The serial link of replay.py, but a line the timeout cuts is kept until its end arrives,
    not dropped: the bridge reads with a short timeout, and a record line now and then takes
    longer than that to come in."""

    partial = b""

    def receive(self, timeout):
        self.port.timeout = timeout
        self.partial += self.port.readline()
        if not self.partial.endswith(b"\n"):
            return None
        line = self.partial[:-1]
        self.partial = b""
        return line


def start_board(link, config):
    """Sends the configuration and the start one at a time; returns the request whose reply was
    not "ok", or None when the board started."""
    for request in start_requests(config):
        link.send([request])
        line = link.receive(START_REPLY_TIMEOUT_S)
        if line is None or replay.strip_frame(line) != "ok":
            return request
    return None


def read_link(link, events, stopped):
    """Moves every line the link delivers onto the events queue, from its own thread, so that
    the main loop waits on messages and lines alike."""
    try:
        while not stopped.is_set():
            line = link.receive(WAIT_S)
            if line is not None:
                events.put(("line", line))
    except Exception:  # pylint: disable=broad-except
        # A serial port closed under the reader at the end raises too; only a break during the
        # run is news.
        if not stopped.is_set():
            events.put(("closed",))


def run(bridge, events, deadline):
    """Returns False when the link closed."""
    while deadline is None or time.monotonic() < deadline:
        try:
            event = events.get(timeout=WAIT_S)
        except queue.Empty:
            continue
        if event[0] == "message":
            bridge.forward(*event[1:])
        elif event[0] == "line":
            bridge.receive(event[1])
        else:
            return False
    return True


def drain(bridge, events):
    """Takes the lines still on the way for DRAIN_S, so that late replies meet their inputs."""
    deadline = time.monotonic() + DRAIN_S
    while bridge.pending and time.monotonic() < deadline:
        try:
            event = events.get(timeout=WAIT_S)
        except queue.Empty:
            continue
        if event[0] == "line":
            bridge.receive(event[1])
        elif event[0] == "closed":
            return


def parse_arguments(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--config", required=True, help="the configuration file (JSON)")
    target = parser.add_mutually_exclusive_group(required=True)
    target.add_argument("--port", help="the serial port of the board")
    target.add_argument("--command", help="the host twin, a program and its arguments as a "
                                          "POSIX shell splits them")
    parser.add_argument("--out", required=True, help="the folder the files are appended to")
    parser.add_argument("--minutes", type=float, help="how long to run; until interrupted "
                                                      "without it")
    parser.add_argument("--replay", help="a capture file to read instead of the broker")
    arguments = parser.parse_args(argv)
    if arguments.minutes is not None and not (math.isfinite(arguments.minutes)
                                              and arguments.minutes > 0):
        parser.error("--minutes must be a positive number")
    return arguments


def open_source(arguments, config, events):
    if arguments.replay is not None:
        return Capture(arguments.replay, events)
    return Broker(config["broker"], events)


def bridge_run(link, config, arguments, output):
    """Starts the board and runs until the deadline or an interrupt; returns the exit status."""
    if not replay.wait_ready(link):
        print("shadow: no ready", file=sys.stderr)
        return 1
    refused = start_board(link, config)
    if refused is not None:
        print("shadow: the board did not take %r" % refused, file=sys.stderr)
        return 1
    events = queue.Queue()
    stopped = threading.Event()
    threading.Thread(target=read_link, args=(link, events, stopped), daemon=True).start()
    source = open_source(arguments, config, events)
    inputs = topic_inputs(config)
    bridge = Bridge(link, output, inputs)
    source.start(inputs)
    deadline = None
    if arguments.minutes is not None:
        deadline = time.monotonic() + arguments.minutes * 60.0
    closed = False
    try:
        closed = not run(bridge, events, deadline)
        if not closed:
            drain(bridge, events)
    except KeyboardInterrupt:
        pass
    finally:
        source.stop()
        stopped.set()
    bridge.finish()
    print(bridge.summary())
    if closed:
        print("shadow: the link closed", file=sys.stderr)
        return 1
    return 0


def main(argv):
    arguments = parse_arguments(argv)
    try:
        config = read_config(arguments.config)
    except ConfigError as error:
        print("shadow: %s: %s" % (arguments.config, error), file=sys.stderr)
        return 2
    output = Output(arguments.out)
    try:
        link = replay.ProcessLink(arguments.command) if arguments.command is not None \
            else SerialLink(arguments.port)
    except Exception as error:  # pylint: disable=broad-except
        print("shadow: cannot open the link: %s" % error, file=sys.stderr)
        output.close()
        return 1
    try:
        return bridge_run(link, config, arguments, output)
    finally:
        link.close()
        output.close()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
