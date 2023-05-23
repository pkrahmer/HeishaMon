import time

import paho.mqtt.client
import traceback

from command import Command, OptionalCommand
from topics import Topic
import paho.mqtt.client as mqtt
import logging


class MQTT:
    def __init__(self, host: str, port: int, topic_base: str, on_command: any,
                 published_topics: [], subscribed_commands: []):
        self.host = host
        self.port = port
        self.topic_base = topic_base
        self.subscribed_commands = subscribed_commands
        self.published_topics = published_topics
        self.on_command = on_command

        self.client = mqtt.Client()
        self.client.on_connect = self.on_mqtt_connect
        self.client.on_disconnect = self.on_mqtt_disconnect
        self.client.on_connect_fail = self.on_mqtt_connect_fail
        self.client.on_message = self.on_message
        self.client.on_log = self.on_mqtt_log

        while True:
            try:
                logging.debug(F"mqtt: connecting to {self.host}:{self.port}")
                self.client.connect(self.host, self.port, 60)
                break
            except ValueError as err:
                logging.critical(F"mqtt: configuration problem {self.host}:{self.port}: {err}")
                exit(1)
            except Exception as err:
                logging.warning(F"mqtt: failed to connect to {self.host}:{self.port}: {err}. Retrying...")
                time.sleep(10)

        self.client.loop_start()

    def publish(self, topic: Topic):
        if topic.name.lower() in self.published_topics:
            self.client.publish(f"{self.topic_base}/{topic.type}/{topic.name}", payload=topic.value, qos=0, retain=True)
        else:
            logging.debug(F"mqtt: skipping deactivated topic {topic.name}")

    def on_message(self, client, userdata, message: paho.mqtt.client.MQTTMessage):
        try:
            name = message.topic
            if name.lower().startswith(F"{self.topic_base}/commands/".lower()):
                name = message.topic[len(F"{self.topic_base}/commands/"):]
            if name.lower() in self.subscribed_commands:
                if message.payload.isdigit():
                    param = int(message.payload)
                    if self.on_command:
                        self.on_command(name, param)
                else:
                    logging.warning(f"mqtt: command {name} only supports integer payload")
            else:
                logging.warning(f"mqtt: command {name}={message.payload} not known or allowed.")
        except Exception as err:
            logging.error(f"mqtt: unknown error handling command {message.topic}={message.payload}: {err}")
            traceback.print_exc(err)

    def on_mqtt_connect(self, client, userdata, flags, rc):
        logging.info(f"mqtt: connected to {self.host}:{self.port} rc={rc}")
        topics = [(F"{self.topic_base}/commands/{command}", 1)
                  for command in Command().known_commands()
                  if command.lower() in self.subscribed_commands]
        topics += [(F"{self.topic_base}/commands/{command}", 1)
                  for command in OptionalCommand().known_commands()
                  if command.lower() in self.subscribed_commands]

        logging.info(F"mqtt: subscribing {len(topics)} topics")
        self.client.subscribe(topics)

    def on_mqtt_disconnect(self, client, userdata, rc):
        logging.warning(f"mqtt: disconnected from {self.host}:{self.port} rc={rc}")

    def on_mqtt_connect_fail(self, client, userdata, rc):
        logging.warning(f"mqtt: failed to connect to  {self.host}:{self.port} rc={rc}")

    def on_mqtt_log(self, client, userdata, level, buf):
        logging.debug(f"mqtt: {buf}")

    def shutdown(self):
        self.client.disconnect()
