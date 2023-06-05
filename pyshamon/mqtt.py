import time
import traceback

from command import Command, OptionalCommand
from topics import Topic
from paho.mqtt.client import Client, MQTTv5, MQTTv31, MQTTv311, MQTTMessage
from paho.mqtt.properties import Properties
from paho.mqtt.packettypes import PacketTypes
import logging
import binascii

mqttVersions = {
    31: MQTTv31,
    311: MQTTv311,
    5: MQTTv5
}


class MQTT:
    def __init__(self, protocol_version: int, host: str, port: int, topic_base: str, on_command: any,
                 published_topics: [], subscribed_commands: [], username: str = None, password: str = None):
        self.host = host
        self.port = port
        self.topic_base = topic_base
        self.subscribed_commands = subscribed_commands
        self.published_topics = published_topics
        self.on_command = on_command
        self.protocol_version = protocol_version

        self.client = Client(protocol=mqttVersions[protocol_version])
        if username is not None:
            self.client.username_pw_set(username, password)
        self.client.on_connect = self.on_mqtt_connect
        self.client.on_disconnect = self.on_mqtt_disconnect
        self.client.on_connect_fail = self.on_mqtt_connect_fail
        self.client.on_message = self.on_message
        self.client.on_log = self.on_mqtt_log

        self.running = False

    def run(self):
        self.running = True
        while self.running:
            try:
                logging.info(F"mqtt: connecting to {self.host}:{self.port} using protocol v{self.protocol_version}")
                self.client.connect(host=self.host, port=self.port, keepalive=60)
                break
            except ValueError as err:
                logging.critical(F"mqtt: configuration problem {self.host}:{self.port}: {err}")
                exit(1)
            except Exception as err:
                logging.warning(F"mqtt: failed to connect to {self.host}:{self.port}: {err}. Retrying...")
                time.sleep(5)

        self.client.loop_start()

    def publish(self, topic: Topic):
        if topic.name.lower() in self.published_topics:
            properties = Properties(PacketTypes.PUBLISH)
            properties.UserProperty = ("dt", str(int(topic.since.timestamp())))
            properties.UserProperty = ("dsc", topic.description)
            self.client.publish(
                f"{self.topic_base}/{topic.type}/{topic.name}",
                payload=topic.value, qos=0, retain=True, properties=properties)
            return True
        else:
            logging.debug(F"mqtt: skipping deactivated topic {topic.name}")
            return False

    def publish_log(self, payload):
        if "log" in self.published_topics:
            self.client.publish(f"{self.topic_base}/log", payload=payload, qos=0, retain=False)

    def publish_raw(self, topic_type: str, raw: []):
        if "raw" in self.published_topics:
            try:
                self.client.publish(f"{self.topic_base}/raw/{topic_type}", payload=binascii.hexlify(bytes(raw), " "), qos=0, retain=False)
            except Exception as err:
                logging.error(f"mqtt: unknown error handling raw data: {err}")

    def on_message(self, client, userdata, message: MQTTMessage):
        try:
            name = message.topic
            if name.lower().startswith(F"{self.topic_base}/commands/".lower()):
                name = message.topic[len(F"{self.topic_base}/commands/"):]
            if name.lower() in self.subscribed_commands:
                try:
                    param = int(message.payload)
                    if self.on_command:
                        self.on_command(name, param)
                except ValueError as err:
                    logging.warning(f"mqtt: command {name} only supports integer payload but got '{message.payload}': {err}")
            else:
                logging.warning(f"mqtt: command {name}={message.payload} not known or allowed.")
        except Exception as err:
            logging.error(f"mqtt: unknown error handling command {message.topic}={message.payload}: {err}")

    def on_mqtt_connect(self, client, userdata, flags, rc, properties=None):
        logging.info(f"mqtt: connected to {self.host}:{self.port} rc={rc}")
        topics = [(F"{self.topic_base}/commands/{command}", 1)
                  for command in Command().known_commands()
                  if command.lower() in self.subscribed_commands]
        topics += [(F"{self.topic_base}/commands/{command}", 1)
                  for command in OptionalCommand().known_commands()
                  if command.lower() in self.subscribed_commands]

        logging.info(F"mqtt: subscribing {len(topics)} topics")
        self.client.subscribe(topics)

    def on_mqtt_disconnect(self, client, userdata, rc, properties=None):
        logging.warning(f"mqtt: disconnected from {self.host}:{self.port} rc={rc}")

    def on_mqtt_connect_fail(self, client, userdata, rc, properties=None):
        logging.warning(f"mqtt: failed to connect to  {self.host}:{self.port} rc={rc}")

    def on_mqtt_log(self, client, userdata, level, buf):
        logging.log(level, f"mqtt: {buf}")

    def shutdown(self):
        self.running = False
        self.client.disconnect()
