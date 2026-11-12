/*
 * The firmware glue: the only code of the control loop that knows HeishaMon. Upstream's sketch
 * calls it from six hook lines; it loads the settings file, hands the decoded pump values and the
 * MQTT inputs to the loop, ticks the loop with the board's clock, publishes the telemetry and
 * sends the router's commands through upstream's command path when commands may reach the pump.
 * Everything it decides is in the pure modules beside it; it only moves values between them and
 * HeishaMon. It never waits, and it allocates nothing of its own after setup.
 */
#ifndef CTL_FIRMWARE_H
#define CTL_FIRMWARE_H

#include <Arduino.h>
#include <PubSubClient.h>

/* Loads /control.json, or the defaults when there is no valid file, and starts the loop cold at
 * the board's clock. Keeps both functions for the commands and the log lines. Refuses nothing: a
 * refused setting or an invalid file is logged and gives the defaults for what it would have
 * set. */
void ctl_firmware_setup(bool (*send_command)(byte *, int), void (*log_message)(char *));

/* Passes the thermostat state of the emulated frame as the pump's confirmation and ticks the loop
 * at the board's clock; after a decision publishes the telemetry under <base>/control/ and sends
 * the commands when they may reach the pump. Returns at once when no tick is due, and does
 * nothing before ctl_firmware_setup. */
void ctl_firmware_loop(PubSubClient &mqtt, const char *base, bool optional_pcb, bool listen_only);

/* Hands outdoor temperature, produced heat, valve and defrost state of a decoded main data frame
 * to the loop at the board's clock. Does nothing before ctl_firmware_setup. */
void ctl_firmware_frame(char *data);

/* Subscribes to the configured room and PV topics and to <base>/control/set/+, then publishes
 * every setting retained under <base>/control/config/. Keeps the client and the base for the
 * messages that follow. Does nothing before ctl_firmware_setup. */
void ctl_firmware_subscribe(PubSubClient &mqtt, const char *base);

/* Returns true when the topic is a room, the PV or a setting of the loop and the message was
 * handled here; false, touching nothing, for every other topic, which upstream then handles as
 * before, and for every topic before ctl_firmware_setup and ctl_firmware_subscribe. */
bool ctl_firmware_mqtt(char *topic, byte *payload, unsigned int length);

#endif
