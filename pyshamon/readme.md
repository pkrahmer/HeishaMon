# PyshaMon - Python HeishaMon Variant

## Abstract

PyshaMon is a pure Python version based on the code of embedded HeishaMon version. 
PyshaMon allows to connect the heat pump directly to the serial port of any computer 
(typically Raspberry Pi) via RS485 modules or a logic level converter circuit.

## Status
- MQTT only (implemented, topics and commands mostly compatible)
- Optional PCB support (implemented)
- Json set command still to be implemented (not planned, instead individual commands are implemented)
- No RAW messages (maybe planned)
- No DS18B20 (not planned)
- No S0 (not planned)
- No HTTP/REST (not planned)

## Usage

Configure your specific requirements in pyshamon.conf. Select `mqtt_commands` you want Pyshamon to 
listen for and `mqtt_topics` it should publish. Then run pyshamon.py:

```python3 pyshamon.py [<config file>]```

## Create a Docker Container

To create a docker container, python3 and python3-venv packages are needed as minimum. 
A virtual environment is created, activated and used to install latest dependencies.

Finally, a docker container is built using preconfigured settings from Dockerfile.
In order to run the container, pyshamon needs to have access to its configuratino file and
mapped serial device (ttyAMA0 in the example). Privileged mode is necessary to access 
hardware ports.
```console
sudo apt-get install python3 python3-venv
python3 -m venv .venv
source .venv/bin/activate
python3 -m pip install paho-mqtt
python3 -m pip install pyserial
python3 -m pip freeze > requirements.txt
docker build --tag pyshamon  .
docker run -v pyshamon.conf:/etc/pyshamon.conf -v /dev/ttyAMA0:/dev/ttyAMA0 --privileged --name pyshamon pyshamon
```