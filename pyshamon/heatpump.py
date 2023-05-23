from datetime import datetime, timedelta

from topics import Topics
from queue import Queue
from command import Command, OptionalCommand
import serial
import logging

minimum_poll_interval = 2


class Heatpump:
    def __init__(self, device: str, poll_interval: int, optional_pcb_poll_interval: int, on_topic_received: any):
        self.topics: Topics = Topics()
        self.device = device
        self.onTopicReceived = on_topic_received
        self.commandQueue = Queue()
        self.optionalCommand = OptionalCommand()
        self.pollInterval = None if poll_interval < 0 else 10 \
            if poll_interval < minimum_poll_interval else poll_interval

        self.optionalPollInterval = None if optional_pcb_poll_interval < 0 else 10 \
            if optional_pcb_poll_interval < minimum_poll_interval else optional_pcb_poll_interval

        self.serial: serial.Serial = None

        self.open_serial()
        if self.pollInterval:
            logging.info(F"heatpump: connected to {self.device} with 9600-8-E-1, poll interval {self.pollInterval}s")
            self.nextPoll = datetime.now() + timedelta(seconds=2)
        else:
            logging.info(F"heatpump: connected to {self.device} with 9600-8-E-1, no polling")
            self.nextPoll = datetime.max

        if self.optionalPollInterval:
            logging.info(F"heatpump: simulating optional pcb with poll interval {self.optionalPollInterval}s")
            self.nextOptionalPoll = datetime.now() + timedelta(seconds=0)
        else:
            self.nextOptionalPoll = datetime.max

        self.nextAllowedSend = datetime.now() + timedelta(seconds=minimum_poll_interval)

    def open_serial(self):
        self.serial = \
            serial.Serial(self.device,
                          baudrate=9600,
                          parity=serial.PARITY_EVEN,
                          stopbits=serial.STOPBITS_ONE,
                          timeout=0.1)

    def on_receive(self, buffer: []):
        if self.topics.decode_and_update(buffer):
            logging.debug(F"Received {len(buffer)} bytes: {buffer}")
            for topic in self.topics.topics:
                if self.onTopicReceived is not None:
                    if self.onTopicReceived(topic):
                        topic.delegated = True

    def shutdown(self):
        logging.info("heatpump: disconnecting")
        self.serial.close()

    def command(self, name: str, param: int):
        command = Command()
        if command.set(name, param):
            self.commandQueue.put(command)
            return True
        else:
            return False

    def optional_command(self, name: str, param: int):
        if self.optionalCommand.set(name, param):
            self.nextOptionalPoll = datetime.now() + timedelta(seconds=minimum_poll_interval)
            return True
        else:
            return False

    def loop(self) -> []:
        buffer = []
        while True:
            try:
                single_or_no_byte = self.serial.read(1)
                if len(single_or_no_byte) > 0:
                    buffer += single_or_no_byte
                else:
                    break
            except serial.serialutil.PortNotOpenError:
                self.open_serial()

        if len(buffer) > 0:
            try:
                self.on_receive(buffer)
            except Exception as err:
                self.nextPoll = datetime.now() + timedelta(seconds=minimum_poll_interval)
                logging.error(F"Unknown error while processing received data: {err}")

        if self.nextAllowedSend < datetime.now():

            if not self.commandQueue.empty():
                try:
                    command: Command = self.commandQueue.get()
                    query: [] = command.command_query()
                    self.nextAllowedSend = datetime.now() + timedelta(seconds=minimum_poll_interval)
                    logging.debug(F"raw command: {query}")
                    self.serial.write(query)
                except Exception as err:
                    logging.error(F"Unknown error while sending command: {err}")

            elif self.nextOptionalPoll < datetime.now():
                try:
                    query: [] = self.optionalCommand.optional_command_query()
                    logging.debug(F"Polling for new optional data {query}")
                    self.nextOptionalPoll = datetime.now() + timedelta(seconds=self.optionalPollInterval)
                    self.nextAllowedSend = datetime.now() + timedelta(seconds=minimum_poll_interval)
                    self.serial.write(query)
                except Exception as err:
                    logging.error(F"Unknown error while polling optional data: {err}")

            elif self.nextPoll < datetime.now():
                try:
                    query: [] = Command().poll_query()
                    logging.debug(F"Polling for new data {query}")
                    self.nextPoll = datetime.now() + timedelta(seconds=self.pollInterval)
                    self.nextAllowedSend = datetime.now() + timedelta(seconds=minimum_poll_interval)
                    self.serial.write(query)
                except Exception as err:
                    logging.error(F"Unknown error while polling: {err}")
