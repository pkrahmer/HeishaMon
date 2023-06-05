import os
import sys

from topics import Topic
import logging
from logging import LogRecord
import atexit
import configparser
from mqtt import MQTT
from heatpump import Heatpump
from datetime import datetime
import json
import signal


def raw_diff(old, new: []):
    hx: str = ""
    changed = False
    for o, n in zip(old, new):
        if o != n:
            hx += "\033[96m\033[1m" if not changed else ""
            changed = True
        else:
            hx += "\033[0m" if changed else ""
            changed = False
        hx += "%0.2X " % n
    if changed:
        hx += "\033[0m"
    return hx


class Pyshamon:
    def __init__(self):
        self.last_raw = {'main': [0] * 203, 'optional': [0] * 20}

        self.config = configparser.ConfigParser(os.environ)
        self.read_config()
        self.cleanedUp = False

        self.log_mqtt_level = self.config.getint("pyshamon", "log_mqtt_level")
        logging.basicConfig(format=self.config.get("pyshamon", "log_format"),
                            level=self.config.getint("pyshamon", "log_level"))
        logging.getLogger().addHandler(self.LogHandler(self.on_log))

        logging.info("pyshamon: starting up")

        atexit.register(self.cleanup)
        signal.signal(signal.SIGINT, self.cleanup)
        signal.signal(signal.SIGTERM, self.cleanup)

        try:
            self.heatpump = Heatpump(self.config.get("heatpump", "serial_port"),
                                     self.config.getint("heatpump", "poll_interval"),
                                     self.config.getint("heatpump", "optional_pcb_poll_interval"),
                                     self.on_topic_received,
                                     self.on_topic_data)
        except Exception as msg:
            logging.error(F"pyshamon: failed to connect to heat pump: {msg}")
            raise msg

        self.mqtt = MQTT(self.config.getint("mqtt", "version"),
                         self.config.get("mqtt", "host"),
                         self.config.getint("mqtt", "port"),
                         self.config.get("mqtt", "topic_base"),
                         self.on_command_received,
                         [key.lower() for (key, value) in self.config.items('mqtt_topics')
                          if value.lower() in ['yes', 'true', '1']],
                         [key.lower() for (key, value) in self.config.items('mqtt_commands')
                          if value.lower() in ['yes', 'true', '1']])
        self.mqtt.run()

        while not self.cleanedUp:
            try:
                self.heatpump.loop()
            except KeyboardInterrupt:
                self.cleanup()
            except Exception as err:
                if not self.cleanedUp:
                    logging.error(err)

    class LogHandler(logging.Handler):
        def __init__(self, on_log: any):
            self.on_log = on_log
            logging.Handler.__init__(self)

        def emit(self, record: LogRecord):
            self.on_log(record)

    def on_log(self, record: LogRecord):
        if self.log_mqtt_level <= record.levelno:
            data = {"time": str(datetime.fromtimestamp(record.created)),
                    "level": record.levelname,
                    "msg": record.msg}
            try:
                self.mqtt.publish_log(json.dumps(data))
            except AttributeError:
                pass

    def read_config(self):
        config_file_candidates = \
            ["/etc/pyshamon.conf", "~/.pyshamon.conf", "pyshamon.conf", sys.argv[1] if len(sys.argv) > 1 else ""]
        self.config.read(config_file_candidates)
        if not self.config.sections():
            print(F"Config file not found. Searched: {config_file_candidates}")
            exit(1)

    def cleanup(self, *args):
        if self.cleanedUp:
            return

        self.cleanedUp = True
        logging.warning("pyshamon: shutting down")
        try:
            self.heatpump.shutdown()
        except Exception as err:
            logging.warning(F"pyshamon: failed to disconnect from heat pump: {err}")

        try:
            self.mqtt.shutdown()
        except Exception as err:
            logging.warning(F"pyshamon: failed to shut down mqtt: {err}")

        pass

    def on_topic_received(self, topic: Topic) -> bool:
        if not topic.delegated:
            if topic.name == "Alarm_State" and topic.value == 1:
                logging.warning(f"pyshamon: heatpump reported alarm state!")
            logging.info(f"topic: {topic}")

            return self.mqtt.publish(topic)

    def on_topic_data(self, topic_type: str, raw: []):
        if raw != self.last_raw[topic_type]:
            diff = raw_diff(self.last_raw[topic_type], raw)
            logging.info(f"raw: {diff}")
            self.mqtt.publish_raw(topic_type, raw)
            self.last_raw[topic_type] = raw

    def on_command_received(self, name: str, param: int):
        if self.heatpump.command(name, param):
            logging.info(f"command: {name} = {param}")
        elif self.heatpump.optional_command(name, param):
            logging.info(f"optional pcb command: {name} = {param}")
        else:
            logging.error(f"command: {name} not implemented!")


if __name__ == '__main__':
    Pyshamon()
