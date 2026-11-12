#include "ctl_firmware.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <PubSubClient.h>

#include "../../../commands.h"
#include "../../../decode.h"

extern "C" {
#include "../core/ctl_controller.h"
#include "../loop/ctl_clock.h"
#include "../loop/ctl_loop.h"
#include "../route/ctl_router.h"
#include "ctl_dispatch.h"
#include "ctl_settings.h"
#include "ctl_telemetry.h"
}

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const char SETTINGS_PATH[] = "/control.json";
// The file is written beside its place and renamed over it, so that a reset while writing never
// leaves half a file behind.
static const char SETTINGS_TEMPORARY_PATH[] = "/control.json.new";

// Upstream's main data frame numbers the values it decodes; these four are the loop's.
static const unsigned int TOPIC_OUTSIDE_TEMP = 14;
static const unsigned int TOPIC_HEAT_POWER_PRODUCTION = 15;
static const unsigned int TOPIC_THREEWAY_VALVE_STATE = 20;
static const unsigned int TOPIC_DEFROSTING_STATE = 26;

// Byte 6 of the emulated optional PCB frame holds the thermostat states.
static const size_t PCB_THERMOSTAT_BYTE = 6;

// Upstream's base has at most 127 characters; "/control/" and a subtopic follow it.
static const size_t TOPIC_SIZE = 192;
// The longest telemetry value is the command list, far below this.
static const size_t VALUE_SIZE = 128;
static const size_t LOG_SIZE = 160;

// Why commands do not reach the pump, or that they do.
typedef enum {
    GATE_UNKNOWN,
    GATE_OPEN,
    GATE_DISABLED,
    GATE_NO_PCB,
    GATE_LISTEN_ONLY
} gate;

static bool started;
static bool (*send_command_function)(byte *, int);
static void (*log_function)(char *);
static PubSubClient *mqtt_client;
static const char *topic_base;

static ctl_settings settings;
static ctl_loop_config config;
static ctl_loop loop_state;
static ctl_clock board_clock;
static ctl_loop_record record;
static gate last_gate = GATE_UNKNOWN;

static char log_line[LOG_SIZE];
static char topic_text[TOPIC_SIZE];
static char subtopic_text[CTL_TELEMETRY_SUBTOPIC_MAX];
static char value_text[VALUE_SIZE];
static char setting_value[CTL_SETTINGS_VALUE_MAX];

static void log_text(const char *format, ...) __attribute__((format(printf, 1, 2)));

static void log_text(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(log_line, sizeof log_line, format, arguments);
    va_end(arguments);
    log_function(log_line);
}

static double now_s()
{
    return (double)ctl_clock_widen(&board_clock, millis()) / 1000.0;
}

static const char *setting_status_name(ctl_setting_status status)
{
    switch (status) {
    case CTL_SETTING_OK:
        return "OK";
    case CTL_SETTING_UNKNOWN:
        return "UNKNOWN";
    case CTL_SETTING_BAD_VALUE:
        return "BAD_VALUE";
    case CTL_SETTING_INVALID_SET:
        return "INVALID_SET";
    }
    return "INVALID";
}

// --- The settings file ---------------------------------------------------------------------

static void load_settings()
{
    ctl_settings_defaults(&settings);
    if (!LittleFS.exists(SETTINGS_PATH)) {
        log_text("control: no %s, using the default settings", SETTINGS_PATH);
        return;
    }
    File file = LittleFS.open(SETTINGS_PATH, "r");
    if (!file) {
        log_text("control: cannot open %s, using the default settings", SETTINGS_PATH);
        return;
    }
    JsonDocument document;
    DeserializationError error = deserializeJson(document, file);
    file.close();
    if (error || !document.is<JsonObject>()) {
        log_text("control: %s is not a JSON object (%s), using the default settings",
                 SETTINGS_PATH, error ? error.c_str() : "not an object");
        return;
    }
    for (JsonPair pair : document.as<JsonObject>()) {
        const char *name = pair.key().c_str();
        if (!pair.value().is<const char *>()) {
            log_text("control: setting %s in %s is not a text, skipped", name, SETTINGS_PATH);
            continue;
        }
        ctl_setting_status status =
            ctl_settings_set(&settings, name, pair.value().as<const char *>());
        if (status != CTL_SETTING_OK) {
            log_text("control: setting %s in %s refused (%s), skipped", name, SETTINGS_PATH,
                     setting_status_name(status));
        }
    }
    if (!ctl_settings_valid(&settings)) {
        log_text("control: the settings of %s are invalid, using the default settings",
                 SETTINGS_PATH);
        ctl_settings_defaults(&settings);
    }
}

static bool put_text(File &file, const char *text)
{
    size_t length = strlen(text);
    return file.write((const uint8_t *)text, length) == length;
}

// A value is printable ASCII, so a quote and a backslash are all that JSON needs escaped.
static bool put_json_string(File &file, const char *text)
{
    bool ok = put_text(file, "\"");
    for (const char *c = text; *c != '\0' && ok; c++) {
        if (*c == '"' || *c == '\\') {
            ok = put_text(file, "\\");
        }
        ok = ok && file.write((uint8_t)*c) == 1;
    }
    return ok && put_text(file, "\"");
}

static bool write_settings(File &file)
{
    char name[CTL_SETTINGS_NAME_MAX];
    bool ok = put_text(file, "{");
    size_t count = ctl_settings_name_count(&settings);
    for (size_t i = 0; i < count && ok; i++) {
        ok = ctl_settings_name_at(&settings, i, name, sizeof name)
             && ctl_settings_get(&settings, name, setting_value, sizeof setting_value)
             && (i == 0 || put_text(file, ",")) && put_json_string(file, name)
             && put_text(file, ":") && put_json_string(file, setting_value);
    }
    return ok && put_text(file, "}\n");
}

static void save_settings()
{
    File file = LittleFS.open(SETTINGS_TEMPORARY_PATH, "w");
    if (!file) {
        log_text("control: cannot write %s, the change is lost at the next start",
                 SETTINGS_TEMPORARY_PATH);
        return;
    }
    bool written = write_settings(file);
    file.close();
    if (!written) {
        LittleFS.remove(SETTINGS_TEMPORARY_PATH);
        log_text("control: writing %s failed, the change is lost at the next start",
                 SETTINGS_TEMPORARY_PATH);
        return;
    }
    if (!LittleFS.rename(SETTINGS_TEMPORARY_PATH, SETTINGS_PATH)) {
        LittleFS.remove(SETTINGS_PATH);
        if (!LittleFS.rename(SETTINGS_TEMPORARY_PATH, SETTINGS_PATH)) {
            log_text("control: cannot replace %s, the change is lost at the next start",
                     SETTINGS_PATH);
        }
    }
}

// --- MQTT ----------------------------------------------------------------------------------

static void publish(const char *subtopic, const char *value, bool retained)
{
    int length = snprintf(topic_text, sizeof topic_text, "%s/control/%s", topic_base, subtopic);
    if (length < 0 || (size_t)length >= sizeof topic_text) {
        return;
    }
    mqtt_client->publish(topic_text, value, retained);
}

static void publish_setting_at(size_t index)
{
    if (ctl_telemetry_setting(&settings, index, subtopic_text, sizeof subtopic_text, value_text,
                              sizeof value_text)) {
        publish(subtopic_text, value_text, true);
    }
}

// A name the settings do not list has no value to answer with, so nothing is published.
static void publish_setting(const char *name)
{
    char listed[CTL_SETTINGS_NAME_MAX];
    size_t count = ctl_settings_name_count(&settings);
    for (size_t i = 0; i < count; i++) {
        bool found = ctl_settings_name_at(&settings, i, listed, sizeof listed);
        if (found && strcmp(listed, name) == 0) {
            publish_setting_at(i);
            return;
        }
    }
}

static void publish_settings()
{
    size_t count = ctl_settings_name_count(&settings);
    for (size_t i = 0; i < count; i++) {
        publish_setting_at(i);
    }
}

static void publish_telemetry(double now, bool enabled)
{
    static const ctl_group groups[] = {CTL_GROUP_INPUTS, CTL_GROUP_STATUS, CTL_GROUP_OUTPUTS};
    ctl_telemetry_source source;
    source.record = &record;
    source.rooms = &loop_state.rooms;
    source.rooms_config = &config.rooms;
    source.uptime_s = now;
    source.enabled = enabled;
    for (ctl_group group : groups) {
        size_t count = ctl_telemetry_count(&source, group);
        for (size_t i = 0; i < count; i++) {
            if (ctl_telemetry_entry(&source, group, i, subtopic_text, sizeof subtopic_text,
                                    value_text, sizeof value_text)) {
                publish(subtopic_text, value_text, false);
            }
        }
    }
}

static void subscribe_one(const char *topic, bool subscribe)
{
    if (subscribe) {
        mqtt_client->subscribe(topic);
    } else {
        mqtt_client->unsubscribe(topic);
    }
}

static void subscribe_inputs(bool subscribe)
{
    for (size_t i = 0; i < settings.room_count && i < CTL_ROOMS_MAX; i++) {
        if (settings.room_topic[i][0] != '\0') {
            subscribe_one(settings.room_topic[i], subscribe);
        }
    }
    if (settings.pv_topic[0] != '\0') {
        subscribe_one(settings.pv_topic, subscribe);
    }
}

// The names whose change moves the set of input topics.
static bool moves_topics(const char *name)
{
    static const char ROOM_TOPIC_SUFFIX[] = ".topic";
    size_t length = strlen(name);
    size_t suffix = sizeof ROOM_TOPIC_SUFFIX - 1;
    return strcmp(name, "rooms") == 0 || strcmp(name, "pv_topic") == 0
           || (length > suffix && strcmp(name + length - suffix, ROOM_TOPIC_SUFFIX) == 0);
}

static void handle_setting(const char *name, const byte *payload, unsigned int length)
{
    ctl_setting_status status = CTL_SETTING_BAD_VALUE;
    bool cooling = settings.cooling;
    bool topics = moves_topics(name);
    // The payload lives in the client's buffer, which the next subscribe or publish overwrites.
    bool fits = length < sizeof setting_value && memchr(payload, '\0', length) == NULL;
    if (fits) {
        memcpy(setting_value, payload, length);
        setting_value[length] = '\0';
    }
    if (topics) {
        subscribe_inputs(false);
    }
    if (fits) {
        status = ctl_settings_set(&settings, name, setting_value);
    }
    if (status == CTL_SETTING_OK) {
        save_settings();
        ctl_settings_to_loop(&settings, &config);
        if (settings.cooling != cooling) {
            ctl_set_allowed(&loop_state.state, config.allowed, now_s(), &config.core);
        }
    } else {
        log_text("control: setting %s refused (%s)", name, setting_status_name(status));
    }
    if (topics) {
        subscribe_inputs(true);
    }
    publish_setting(name);
}

// --- Commands ------------------------------------------------------------------------------

static gate gate_of(bool optional_pcb, bool listen_only)
{
    if (!settings.enabled) {
        return GATE_DISABLED;
    }
    if (!optional_pcb) {
        return GATE_NO_PCB;
    }
    if (listen_only) {
        return GATE_LISTEN_ONLY;
    }
    return GATE_OPEN;
}

static const char *gate_text(gate reason)
{
    switch (reason) {
    case GATE_OPEN:
        return "commands go to the pump";
    case GATE_DISABLED:
        return "no commands to the pump: enabled is 0";
    case GATE_NO_PCB:
        return "no commands to the pump: the optional PCB emulation is off";
    case GATE_LISTEN_ONLY:
        return "no commands to the pump: HeishaMon is in listen-only mode";
    case GATE_UNKNOWN:
        break;
    }
    return "commands to the pump unknown";
}

// While the gate was shut the router went on as if it sent, and may have given up on a state
// the pump never got; reopened, it sends the wanted state once, still paced from its last send.
static void watch_gate(gate current)
{
    if (current == last_gate) {
        return;
    }
    if (current == GATE_OPEN && last_gate != GATE_UNKNOWN) {
        ctl_router_reopen(&loop_state.router);
    }
    last_gate = current;
    log_text("control: %s", gate_text(current));
}

static void send_commands(bool optional_pcb)
{
    for (size_t i = 0; i < record.commands.count && i < CTL_ROUTER_COMMANDS_MAX; i++) {
        ctl_command *command = &record.commands.command[i];
        send_heatpump_command(command->name, command->value, send_command_function, log_function,
                              optional_pcb);
    }
}

// --- The hooks -----------------------------------------------------------------------------

void ctl_firmware_setup(bool (*send_command)(byte *, int), void (*log_message)(char *))
{
    send_command_function = send_command;
    log_function = log_message;
    ctl_clock_init(&board_clock);
    load_settings();
    ctl_settings_to_loop(&settings, &config);
    ctl_loop_init(&loop_state, &config, now_s());
    started = true;
}

void ctl_firmware_loop(PubSubClient &mqtt, const char *base, bool optional_pcb, bool listen_only)
{
    if (!started) {
        return;
    }
    mqtt_client = &mqtt;
    topic_base = base;
    gate current = gate_of(optional_pcb, listen_only);
    watch_gate(current);
    // Only an emulated frame that goes out is what the pump is told.
    bool known = optional_pcb && !listen_only;
    ctl_loop_confirm(&loop_state, known,
                     ctl_dispatch_thermostat_state(optionalPCBQuery[PCB_THERMOSTAT_BYTE]));
    double now = now_s();
    if (!ctl_loop_tick(&loop_state, &config, now, &record)) {
        return;
    }
    if (mqtt.connected()) {
        publish_telemetry(now, current == GATE_OPEN);
    }
    if (current == GATE_OPEN) {
        send_commands(optional_pcb);
    }
}

static void hand_value(ctl_input input, char *data, unsigned int topic_number, double now)
{
    String value = getDataValue(data, topic_number);
    ctl_loop_input(&loop_state, &config, input, 0, value.c_str(), value.length(), now);
}

void ctl_firmware_frame(char *data)
{
    if (!started) {
        return;
    }
    double now = now_s();
    hand_value(CTL_INPUT_OUTDOOR, data, TOPIC_OUTSIDE_TEMP, now);
    hand_value(CTL_INPUT_HEAT, data, TOPIC_HEAT_POWER_PRODUCTION, now);
    hand_value(CTL_INPUT_VALVE, data, TOPIC_THREEWAY_VALVE_STATE, now);
    hand_value(CTL_INPUT_DEFROST, data, TOPIC_DEFROSTING_STATE, now);
}

void ctl_firmware_subscribe(PubSubClient &mqtt, const char *base)
{
    if (!started) {
        return;
    }
    mqtt_client = &mqtt;
    topic_base = base;
    subscribe_inputs(true);
    int length = snprintf(topic_text, sizeof topic_text, "%s/control/set/+", base);
    if (length > 0 && (size_t)length < sizeof topic_text) {
        mqtt.subscribe(topic_text);
    }
    publish_settings();
}

bool ctl_firmware_mqtt(char *topic, byte *payload, unsigned int length)
{
    if (!started || mqtt_client == NULL || topic_base == NULL) {
        return false;
    }
    ctl_topic target = ctl_dispatch_topic(&settings, topic_base, topic);
    switch (target.kind) {
    case CTL_TOPIC_INPUT:
        ctl_loop_input(&loop_state, &config, target.input, target.room, (const char *)payload,
                       length, now_s());
        return true;
    case CTL_TOPIC_SETTING:
        handle_setting(target.name, payload, length);
        return true;
    case CTL_TOPIC_NONE:
        break;
    }
    return false;
}
