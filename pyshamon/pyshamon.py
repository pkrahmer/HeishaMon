import sys

from topics import Topic
import logging
import atexit
import configparser
from mqtt import MQTT
from heatpump import Heatpump


class Pyshamon:
    def __init__(self):
        self.heatpump: Heatpump
        self.mqtt: MQTT

        self.config = configparser.ConfigParser()
        self.read_config()

        logging.basicConfig(format=self.config.get("pyshamon", "log_format"),
                            level=self.config.getint("pyshamon", "log_level"))
        logging.info("pyshamon: starting up")

        atexit.register(self.cleanup)

        self.heatpump = Heatpump(self.config.get("heatpump", "serial_port"),
                                 self.config.getint("heatpump", "poll_interval"),
                                 self.config.getint("heatpump", "optional_pcb_poll_interval"),
                                 self.on_topic_received)
        self.mqtt = MQTT(self.config.get("mqtt", "host"),
                         self.config.getint("mqtt", "port"),
                         self.config.get("mqtt", "topic_base"),
                         self.on_command_received,
                         [key.lower() for (key, value) in self.config.items('mqtt_topics')
                          if value.lower() in ['yes', 'true', '1']],
                         [key.lower() for (key, value) in self.config.items('mqtt_commands')
                          if value.lower() in ['yes', 'true', '1']])

        while True:
            self.heatpump.loop()

    def read_config(self):
        config_file_candidates = \
            ["/etc/pyshamon.conf", "~/.pyshamon.conf", "pyshamon.conf", sys.argv[1] if len(sys.argv) > 1 else ""]
        self.config.read(config_file_candidates)
        if not self.config.sections():
            print(F"Config file not found. Searched: {config_file_candidates}")
            exit(1)

    def cleanup(self):
        logging.info("pyshamon: shutting down")
        try:
            if self.mqtt:
                self.mqtt.shutdown()
        except Exception as err:
            logging.warning(F"pyshamon: failed to shut down mqtt: {err}")

        try:
            if self.heatpump:
                self.heatpump.shutdown()
        except Exception as err:
            logging.warning(F"pyshamon: failed to disconnect from heat pump: {err}")

        pass

    def on_topic_received(self, topic: Topic) -> bool:
        if not topic.delegated:
            logging.info(f"topic: {topic}")
            self.mqtt.publish(topic)
            return True

    def on_command_received(self, name: str, param: int):
        if self.heatpump.command(name, param):
            logging.info(f"command: {name} = {param}")
        elif self.heatpump.optional_command(name, param):
            logging.info(f"optional pcb command: {name} = {param}")
        else:
            logging.error(f"command: {name} not implemented!")


if __name__ == '__main__':
    Pyshamon()
