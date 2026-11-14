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
#include "ctl_integral_store.h"
#include "ctl_settings.h"
#include "ctl_telemetry.h"
}

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// Each file is written beside its place and renamed over it, so that a reset while writing never
// leaves half a file behind.
static const char SETTINGS_PATH[] = "/control.json";
static const char SETTINGS_TEMPORARY_PATH[] = "/control.json.new";
// The learned integral lives apart from the settings, which change only when a setting does.
static const char INTEGRAL_PATH[] = "/control_integral.txt";
static const char INTEGRAL_TEMPORARY_PATH[] = "/control_integral.txt.new";
// Holds the integral file's text; a longer file is no number the store writes, and is refused.
static const size_t INTEGRAL_FILE_MAX = 32;

// Upstream's main data frame numbers the values it decodes; these four are the loop's.
static const unsigned int TOPIC_OUTSIDE_TEMP = 14;
static const unsigned int TOPIC_HEAT_POWER_PRODUCTION = 15;
static const unsigned int TOPIC_THREEWAY_VALVE_STATE = 20;
static const unsigned int TOPIC_DEFROSTING_STATE = 26;

// Byte 6 of the optional PCB frame holds the thermostat states and the Smart Grid mode.
static const size_t PCB_STATE_BYTE = 6;
// The other board sends its optional PCB frame every second; one older than this no longer says
// what the pump is told.
static const double REMOTE_PCB_FRESH_S = 60.0;

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
// What the telemetry of the last decision was made from, for the page.
static bool decided;
static double decision_s;
static ctl_gate decision_gate;

// The frames rebuilt from another HeishaMon's log, and what became of them.
static ctl_framelog frame_log;
static uint32_t frames_decoded;
static uint32_t frames_dropped;
// Byte 6 of the last optional PCB frame the other board was seen sending, and when.
static bool remote_pcb_seen;
static double remote_pcb_seen_s;
static uint8_t remote_pcb_byte;

// The integral as last written or loaded, and when the last write was tried (or the boot).
static double integral_saved_w;
static double integral_write_s;
static char integral_text[CTL_INTEGRAL_STORE_TEXT_MAX];

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

// Returns false, with the old file kept, when the temporary file cannot be written; the
// consequence ends each log line.
static bool replace_file(const char *path, const char *temporary_path, bool (*write)(File &),
                         const char *consequence)
{
    File file = LittleFS.open(temporary_path, "w");
    if (!file) {
        log_text("control: cannot write %s, %s", temporary_path, consequence);
        return false;
    }
    bool written = write(file);
    file.close();
    if (!written) {
        LittleFS.remove(temporary_path);
        log_text("control: writing %s failed, %s", temporary_path, consequence);
        return false;
    }
    if (!LittleFS.rename(temporary_path, path)) {
        LittleFS.remove(path);
        if (!LittleFS.rename(temporary_path, path)) {
            log_text("control: cannot replace %s, %s", path, consequence);
            return false;
        }
    }
    return true;
}

static void save_settings()
{
    replace_file(SETTINGS_PATH, SETTINGS_TEMPORARY_PATH, write_settings,
                 "the change is lost at the next start");
}

// --- The integral file ---------------------------------------------------------------------

// Nothing is written at boot: a missing or refused file starts the integral at 0, which is the
// value a cold start has anyway.
static void load_integral(double now)
{
    char text[INTEGRAL_FILE_MAX];
    size_t length = 0;
    integral_write_s = now;
    if (LittleFS.exists(INTEGRAL_PATH)) {
        File file = LittleFS.open(INTEGRAL_PATH, "r");
        if (file && file.size() <= sizeof text) {
            length = file.read((uint8_t *)text, sizeof text);
        }
        if (file) {
            file.close();
        }
    }
    double start_w = ctl_integral_store_start(text, length, config.core.integral_limit_w);
    loop_state.state.p_integral_w = start_w;
    integral_saved_w = start_w;
    if (ctl_format_fixed(start_w, 1, integral_text, sizeof integral_text)) {
        log_text("control: integral starts at %s W", integral_text);
    }
}

static bool write_integral(File &file)
{
    return put_text(file, integral_text);
}

// A failed write keeps the old file and counts as a write for the interval, so that a broken
// flash is not hammered at every tick.
static void store_integral(bool reset, double now)
{
    double integral_w = loop_state.state.p_integral_w;
    if (!ctl_integral_store_due(integral_w, integral_saved_w, now - integral_write_s, reset)) {
        return;
    }
    integral_write_s = now;
    if (!ctl_integral_store_format(integral_w, integral_text, sizeof integral_text)) {
        return;
    }
    if (replace_file(INTEGRAL_PATH, INTEGRAL_TEMPORARY_PATH, write_integral,
                     "the integral is tried again later")) {
        integral_saved_w = integral_w;
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
    if (ctl_format_fixed(integral_saved_w, 1, value_text, sizeof value_text)) {
        publish("status/integral_saved_w", value_text, false);
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
    const char *const named[] = {settings.pv_topic, settings.frame_log_topic};
    for (const char *topic : named) {
        if (topic[0] != '\0') {
            subscribe_one(topic, subscribe);
        }
    }
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
static bool change_smart_grid;
static char change_command_topic[CTL_SETTINGS_TOPIC_MAX + 1];

void ctl_firmware_change_begin(bool topics, bool every_ok)
{
    change_cooling = settings.cooling;
    change_smart_grid = settings.smart_grid;
    snprintf(change_command_topic, sizeof change_command_topic, "%s", settings.command_topic);
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
        // What the router sent was another kind of state or went another way, so it says
        // nothing of what the pump now holds; reopened, the router sends once, still paced.
        if (settings.smart_grid != change_smart_grid
            || strcmp(settings.command_topic, change_command_topic) != 0) {
            ctl_router_reopen(&loop_state.router);
        }
    }
    if (change_topics) {
        subscribe_inputs(true);
        // A frame half read from the old log topic must not be finished from the new one, nor
        // may a frame of the old one confirm a command.
        ctl_framelog_init(&frame_log);
        remote_pcb_seen = false;
    }
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

// Only a frame that broke counts as dropped: a whole frame that is no main data frame (an optional
// PCB answer, the extra data block) arrives every second while the other board's emulation is on,
// and counting it would hide a log that loses lines. A frame the other board sent counts as neither.
static void handle_frame_log(const byte *payload, unsigned int length)
{
    ctl_framelog_result result = ctl_framelog_line(&frame_log, (const char *)payload, length);
    if (result == CTL_FRAMELOG_NOTHING) {
        return;
    }
    if (result == CTL_FRAMELOG_SENT) {
        if (ctl_framelog_is_optional_pcb(frame_log.frame, frame_log.length)) {
            remote_pcb_seen = true;
            remote_pcb_seen_s = now_s();
            remote_pcb_byte = frame_log.frame[PCB_STATE_BYTE];
        }
        return;
    }
    if (result == CTL_FRAMELOG_DROPPED) {
        frames_dropped++;
        return;
    }
    if (!ctl_framelog_is_main_data(frame_log.frame, frame_log.length)) {
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
    case CTL_GATE_NO_FRAME_LOG:
        return "no commands to the pump: command_topic is set, frame_log_topic is empty";
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

static bool commands_remote()
{
    return settings.command_topic[0] != '\0';
}

// The other board takes <its base>/commands/<name> as a command of its own web page; a retained
// command would run again at its every reconnect.
static void publish_command(const ctl_command *command)
{
    int length = snprintf(topic_text, sizeof topic_text, "%s/%s", settings.command_topic,
                          command->name);
    if (length < 0 || (size_t)length >= sizeof topic_text) {
        log_text("control: command topic too long for %s, not published", command->name);
        return;
    }
    if (!control_client->publish(topic_text, command->value, false)) {
        log_text("control: publish failed %s %s", topic_text, command->value);
        return;
    }
    log_text("control: published %s %s", topic_text, command->value);
}

static void send_commands(bool optional_pcb)
{
    for (size_t i = 0; i < record.commands.count && i < CTL_ROUTER_COMMANDS_MAX; i++) {
        ctl_command *command = &record.commands.command[i];
        if (commands_remote()) {
            publish_command(command);
        } else {
            send_heatpump_command(command->name, command->value, send_command_function,
                                  log_function, optional_pcb);
        }
    }
}

// What the pump is told: with commands over MQTT, the other board's optional PCB frame as its log
// shows it; otherwise this board's emulated frame, which counts only while it goes out.
static void confirm(bool optional_pcb, bool listen_only, double now)
{
    bool known;
    uint8_t state_byte;
    if (commands_remote()) {
        known = remote_pcb_seen && now - remote_pcb_seen_s <= REMOTE_PCB_FRESH_S;
        state_byte = remote_pcb_byte;
    } else {
        known = optional_pcb && !listen_only;
        state_byte = optionalPCBQuery[PCB_STATE_BYTE];
    }
    int reported = settings.smart_grid ? ctl_dispatch_smart_grid_mode(state_byte)
                                       : ctl_dispatch_thermostat_state(state_byte);
    ctl_loop_confirm(&loop_state, known, reported);
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
    double now = now_s();
    ctl_loop_init(&loop_state, &config, now);
    load_integral(now);
    started = true;
}

void ctl_firmware_loop(PubSubClient &mqtt, const char *base, bool optional_pcb, bool listen_only)
{
    if (!started) {
        return;
    }
    control_client = &mqtt;
    topic_base = base;
    ctl_gate current = ctl_gate_state(settings.enabled, listen_only, optional_pcb,
                                      commands_remote(), settings.frame_log_topic[0] != '\0');
    watch_gate(current);
    double now = now_s();
    confirm(optional_pcb, listen_only, now);
    // A blocked board sends nothing, so nothing could confirm; without assuming the landing its
    // router would repeat and report a fault of a link it never used.
    config.assume_landed = current != CTL_GATE_OPEN;
    if (!ctl_loop_tick(&loop_state, &config, now, &record)) {
        return;
    }
    store_integral(false, now);
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
    case CTL_TOPIC_INTEGRAL_RESET:
        // A command, not a setting: the payload means nothing, and only the integral's own file is
        // written, at once.
        ctl_reset_integral(&loop_state.state, now_s(), &config.core);
        log_text("control: integral reset");
        store_integral(true, now_s());
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
