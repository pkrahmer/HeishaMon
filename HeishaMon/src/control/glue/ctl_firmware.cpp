#include "ctl_firmware.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <PubSubClient.h>

#include "../../../commands.h"
#include "../../../decode.h"
#include "../../../webfunctions.h"

extern "C" {
#include "../core/ctl_controller.h"
#include "../loop/ctl_clock.h"
#include "../loop/ctl_loop.h"
#include "../route/ctl_router.h"
#include "ctl_dispatch.h"
#include "ctl_framelog.h"
#include "ctl_gate.h"
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

// HeishaMon's own globals: a frame from another board's log is decoded exactly as a frame of this
// board's pump, into the same values and under the same base.
extern char actData[];
extern PubSubClient mqtt_client;
extern settingsStruct heishamonSettings;
extern void log_message(char *string);

static bool started;
static bool (*send_command_function)(byte *, int);
static void (*log_function)(char *);
// Named apart from upstream's global mqtt_client, which the frame log's decoding takes.
static PubSubClient *control_client;
static const char *topic_base;

static ctl_settings settings;
static ctl_loop_config config;
static ctl_loop loop_state;
static ctl_clock board_clock;
static ctl_loop_record record;
// The gate of the last pass; none before the first, so that the first pass logs it.
static bool gate_known;
static ctl_gate last_gate;
// Where the pump's values came from when last logged, so that only a change is logged again.
static bool pump_source_logged;
static bool pump_from_mqtt;
// What the telemetry of the last decision was made from, for the page.
static bool decided;
static double decision_s;
static ctl_gate decision_gate;

// The frames rebuilt from another HeishaMon's log, and what became of them.
static ctl_framelog frame_log;
static uint32_t frames_decoded;
static uint32_t frames_dropped;

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
    control_client->publish(topic_text, value, retained);
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

static void publish_telemetry()
{
    static const ctl_group groups[] = {CTL_GROUP_INPUTS, CTL_GROUP_STATUS, CTL_GROUP_OUTPUTS};
    ctl_telemetry_source source;
    if (!ctl_firmware_last_decision(&source)) {
        return;
    }
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
        control_client->subscribe(topic);
    } else {
        control_client->unsubscribe(topic);
    }
}

static void subscribe_inputs(bool subscribe)
{
    for (size_t i = 0; i < settings.room_count && i < CTL_ROOMS_MAX; i++) {
        if (settings.room_topic[i][0] != '\0') {
            subscribe_one(settings.room_topic[i], subscribe);
        }
    }
    const char *const named[] = {settings.pv_topic,      settings.outdoor_topic,
                                 settings.heat_topic,    settings.valve_topic,
                                 settings.defrost_topic, settings.frame_log_topic};
    for (const char *topic : named) {
        if (topic[0] != '\0') {
            subscribe_one(topic, subscribe);
        }
    }
}

static void log_pump_source()
{
    bool from_mqtt = ctl_settings_pump_from_mqtt(&settings);
    if (pump_source_logged && from_mqtt == pump_from_mqtt) {
        return;
    }
    pump_source_logged = true;
    pump_from_mqtt = from_mqtt;
    log_text(from_mqtt ? "control: the pump values come from MQTT, the frames hand none"
                       : "control: the pump values come from this board's frames");
}

static bool ends_with(const char *text, const char *suffix)
{
    size_t length = strlen(text);
    size_t suffix_length = strlen(suffix);
    return length > suffix_length && strcmp(text + length - suffix_length, suffix) == 0;
}

// The names whose change moves the set of input topics: the count of rooms, a room's topic and
// every other topic setting.
static bool moves_topics(const char *name)
{
    return strcmp(name, "rooms") == 0 || ends_with(name, ".topic") || ends_with(name, "_topic");
}

// A change begun here ends with ctl_firmware_change_end; between them only the settings move.
static bool change_cooling;
static bool change_topics;
static bool change_any;
static bool change_every_ok;

void ctl_firmware_change_begin(bool topics, bool every_ok)
{
    change_cooling = settings.cooling;
    change_any = false;
    change_every_ok = every_ok;
    // Unsubscribing needs the old topics, which the change is about to replace.
    change_topics = topics && control_client != NULL;
    if (change_topics) {
        subscribe_inputs(false);
    }
}

ctl_setting_status ctl_firmware_change(const char *name, const char *value, bool *changed)
{
    char before[CTL_SETTINGS_VALUE_MAX];
    bool known = ctl_settings_get(&settings, name, before, sizeof before);
    ctl_setting_status status = ctl_settings_set(&settings, name, value);
    *changed = false;
    if (status == CTL_SETTING_OK) {
        *changed = !known || !ctl_settings_get(&settings, name, setting_value, sizeof setting_value)
                   || strcmp(before, setting_value) != 0;
        change_any = change_any || *changed || change_every_ok;
    } else {
        log_text("control: setting %s refused (%s)", name, setting_status_name(status));
    }
    return status;
}

void ctl_firmware_change_end()
{
    if (change_any) {
        save_settings();
        ctl_settings_to_loop(&settings, &config);
        if (settings.cooling != change_cooling) {
            ctl_set_allowed(&loop_state.state, config.allowed, now_s(), &config.core);
        }
    }
    if (change_topics) {
        subscribe_inputs(true);
        // A frame half read from the old log topic must not be finished from the new one.
        ctl_framelog_init(&frame_log);
    }
    log_pump_source();
}

// The payload lives in the client's buffer, which the unsubscribe of a topic setting, a publish
// or a log line overwrites, so it is copied, or refused, before anything else runs.
static void handle_setting(const char *name, const byte *payload, unsigned int length)
{
    char value[CTL_SETTINGS_VALUE_MAX];
    bool fits = length < sizeof value && memchr(payload, '\0', length) == NULL;
    if (fits) {
        memcpy(value, payload, length);
        value[length] = '\0';
    }
    bool changed;
    // A setting topic has always saved on every OK, the same value included.
    ctl_firmware_change_begin(moves_topics(name), true);
    if (fits) {
        ctl_firmware_change(name, value, &changed);
    } else {
        log_text("control: setting %s refused (%s)", name,
                 setting_status_name(CTL_SETTING_BAD_VALUE));
    }
    ctl_firmware_change_end();
    publish_setting(name);
}

// --- Frames from another HeishaMon's log ------------------------------------------------------

// Every frame the log shows whole but that is no main data frame (an optional PCB answer, the
// extra data block) counts as dropped too: it is read and then thrown away.
static void handle_frame_log(const byte *payload, unsigned int length)
{
    ctl_framelog_result result = ctl_framelog_line(&frame_log, (const char *)payload, length);
    if (result == CTL_FRAMELOG_NOTHING) {
        return;
    }
    if (result == CTL_FRAMELOG_DROPPED
        || !ctl_framelog_is_main_data(frame_log.frame, frame_log.length)) {
        frames_dropped++;
        return;
    }
    frames_decoded++;
    char *data = (char *)frame_log.frame;
    decode_heatpump_data(data, actData, mqtt_client, log_message,
                         heishamonSettings.mqtt_topic_base, heishamonSettings.updateAllTime);
    ctl_firmware_frame(data);
}

// --- Commands ------------------------------------------------------------------------------

static const char *gate_text(ctl_gate reason)
{
    switch (reason) {
    case CTL_GATE_OPEN:
        return "commands go to the pump";
    case CTL_GATE_DISABLED:
        return "no commands to the pump: enabled is 0";
    case CTL_GATE_LISTEN_ONLY:
        return "no commands to the pump: HeishaMon is in listen-only mode";
    case CTL_GATE_NO_OPTIONAL_PCB:
        return "no commands to the pump: the optional PCB emulation is off";
    }
    return "commands to the pump unknown";
}

// While the gate was shut the router went on as if it sent and its commands landed, so it may
// believe in a state the pump never got; reopened, it sends the wanted state once, still paced
// from its last send.
static void watch_gate(ctl_gate current)
{
    if (gate_known && current == last_gate) {
        return;
    }
    if (current == CTL_GATE_OPEN && gate_known) {
        ctl_router_reopen(&loop_state.router);
    }
    gate_known = true;
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
    ctl_framelog_init(&frame_log);
    load_settings();
    ctl_settings_to_loop(&settings, &config);
    ctl_loop_init(&loop_state, &config, now_s());
    started = true;
    log_pump_source();
}

void ctl_firmware_loop(PubSubClient &mqtt, const char *base, bool optional_pcb, bool listen_only)
{
    if (!started) {
        return;
    }
    control_client = &mqtt;
    topic_base = base;
    ctl_gate current = ctl_gate_state(settings.enabled, listen_only, optional_pcb);
    watch_gate(current);
    // Only an emulated frame that goes out is what the pump is told.
    bool known = optional_pcb && !listen_only;
    ctl_loop_confirm(&loop_state, known,
                     ctl_dispatch_thermostat_state(optionalPCBQuery[PCB_THERMOSTAT_BYTE]));
    // A blocked board sends nothing, so nothing could confirm; without assuming the landing its
    // router would repeat and report a fault of a link it never used.
    config.assume_landed = current != CTL_GATE_OPEN;
    double now = now_s();
    if (!ctl_loop_tick(&loop_state, &config, now, &record)) {
        return;
    }
    decided = true;
    decision_s = now;
    decision_gate = current;
    if (mqtt.connected()) {
        publish_telemetry();
    }
    if (current == CTL_GATE_OPEN) {
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
    // A board that shadows another HeishaMon takes the pump's values from MQTT alone; a frame
    // from a pump of its own would otherwise mix two pumps in one loop.
    if (!started || ctl_settings_pump_from_mqtt(&settings)) {
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
    control_client = &mqtt;
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
    // The topic and the payload live in the client's single buffer, which every publish,
    // subscribe, unsubscribe and MQTT log line overwrites: nothing that can send may run before
    // they are read or copied. The dispatch copies the setting's name out of the topic.
    if (!started || control_client == NULL || topic_base == NULL) {
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
    case CTL_TOPIC_FRAME_LOG:
        handle_frame_log(payload, length);
        return true;
    case CTL_TOPIC_NONE:
        break;
    }
    return false;
}

const ctl_settings *ctl_firmware_settings()
{
    return started ? &settings : NULL;
}

bool ctl_firmware_last_decision(ctl_telemetry_source *source)
{
    if (!started || !decided) {
        return false;
    }
    source->record = &record;
    source->rooms = &loop_state.rooms;
    source->rooms_config = &config.rooms;
    source->uptime_s = decision_s;
    source->enabled = decision_gate == CTL_GATE_OPEN;
    source->gate = decision_gate;
    source->frames_decoded = frames_decoded;
    source->frames_dropped = frames_dropped;
    return true;
}

void ctl_firmware_publish_settings()
{
    if (started && control_client != NULL && topic_base != NULL && control_client->connected()) {
        publish_settings();
    }
}
