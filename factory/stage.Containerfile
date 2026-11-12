# The tools this project's make check needs beyond the factory's base image: gcc and g++ for the
# host tests, arduino-cli with the ESP8266 and ESP32 cores for the device sketches and the firmware,
# and HeishaMon's libraries at the versions upstream's CI resolved on 2026-11-12. Only develop counts;
# the build sees no files of the repository.
ARG FACTORY_BASE
FROM ${FACTORY_BASE}
ARG ARDUINO_CLI_VERSION=1.1.1
ARG ARDUINO_CLI_SHA256=2b807add5c3a518861bfd4d3e78de9e02b0a2723df3d90386df2200a767349c4
ARG ESP8266_CORE_VERSION=3.1.2
ARG ESP32_CORE_VERSION=3.3.11
RUN apt-get update \
 && apt-get install -y --no-install-recommends build-essential \
 && rm -rf /var/lib/apt/lists/*
# The board tools live under /opt/arduino, writable by every identity a stage runs under: the
# ESP8266 core writes a header into its own folder at every build. Each container has its own copy.
# arduino-cli verifies the core's archives against the checksums of the package index.
ENV ARDUINO_DIRECTORIES_DATA=/opt/arduino/data \
    ARDUINO_DIRECTORIES_DOWNLOADS=/opt/arduino/downloads \
    ARDUINO_DIRECTORIES_USER=/opt/arduino/user \
    ARDUINO_BOARD_MANAGER_ADDITIONAL_URLS=https://arduino.esp8266.com/stable/package_esp8266com_index.json
RUN curl -fsSL -o /tmp/arduino-cli.tgz \
      "https://github.com/arduino/arduino-cli/releases/download/v${ARDUINO_CLI_VERSION}/arduino-cli_${ARDUINO_CLI_VERSION}_Linux_64bit.tar.gz" \
 && echo "${ARDUINO_CLI_SHA256}  /tmp/arduino-cli.tgz" | sha256sum -c - \
 && tar -xzf /tmp/arduino-cli.tgz -C /usr/local/bin arduino-cli \
 && rm /tmp/arduino-cli.tgz \
 && arduino-cli core update-index \
 && arduino-cli core install "esp8266:esp8266@${ESP8266_CORE_VERSION}" \
 && arduino-cli core install "esp32:esp32@${ESP32_CORE_VERSION}" \
 && arduino-cli lib install "RingBuffer@1.0.5" "PubSubClient@2.8" "ArduinoJson@7.4.3" \
      "DallasTemperature@4.0.6" "OneWire@2.3.8" "Adafruit NeoPixel@1.15.5" "Async TCP@3.5.0" \
 && ARDUINO_LIBRARY_ENABLE_UNSAFE_INSTALL=true arduino-cli lib install \
      --git-url https://github.com/eModbus/eModbus.git#8cdbcc9c28efad2ad7fbb140af8fa6c6e69bf0fa \
 && rm -rf /opt/arduino/downloads \
 && mkdir -p /opt/arduino/downloads /opt/arduino/user \
 && chmod -R a+rwX /opt/arduino
