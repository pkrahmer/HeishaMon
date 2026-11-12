/*
 * The firmware glue: the only code of the control loop that knows HeishaMon. Upstream's sketch
 * calls it from six hook lines; it loads the settings file, hands the decoded pump values and the
 * MQTT inputs to the loop, ticks the loop with the board's clock, publishes the telemetry and
 * sends the router's commands through upstream's command path when commands may reach the pump.
 * Everything it decides is in the pure modules beside it; it only moves values between them and
 * HeishaMon. It never waits, and it allocates nothing of its own after setup. The control page
 * reads the settings and the last decision here, and changes the settings the way a setting
 * topic does.
 */
#ifndef CTL_FIRMWARE_H
#define CTL_FIRMWARE_H

#include <Arduino.h>
#include <PubSubClient.h>

extern "C" {
#include "ctl_settings.h"
#include "ctl_telemetry.h"
}

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

/* Returns the settings in use, or NULL before ctl_firmware_setup. */
const ctl_settings *ctl_firmware_settings();

/* Writes the source of the last decision's telemetry, the one published with it, and returns
 * true. Refuses, returning false and writing nothing, before setup and before the first
 * decision. The source points into the glue's state and stays current until the next tick. */
bool ctl_firmware_last_decision(ctl_telemetry_source *source);

/* A change of settings is ctl_firmware_change_begin, any number of ctl_firmware_change and
 * ctl_firmware_change_end, with nothing in between that ticks the loop. With topics true, begin
 * unsubscribes the input topics and end subscribes the new ones; a change that may move a topic
 * must pass true. Begin and end refuse nothing; call them only after ctl_firmware_setup. */
void ctl_firmware_change_begin(bool topics);

/* Applies one setting with ctl_settings_set and returns its answer; a refusal is logged and
 * keeps the old value. Sets *changed when the setting's value text differs after an OK. */
ctl_setting_status ctl_firmware_change(const char *name, const char *value, bool *changed);

/* When a change since begin altered a value: saves /control.json and applies the settings from
 * the next tick, setting the allowed demands at once when cooling changed. */
void ctl_firmware_change_end();

/* Publishes every listed setting retained under <base>/control/config/, as at a connect. Does
 * nothing before ctl_firmware_subscribe or while the client is not connected. */
void ctl_firmware_publish_settings();

#endif
