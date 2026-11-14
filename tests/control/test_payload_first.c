/*
 * The firmware glue's MQTT handlers read the message from the client's single buffer, which
 * anything that sends (a publish, a subscribe, an unsubscribe, a log line published over MQTT)
 * overwrites. The glue is a .cpp file that only the firmware build compiles, so this test holds
 * the order in its source: in each handler, every read of the payload comes before the first call
 * that can send.
 */
#include "check.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define SOURCE_PATH "HeishaMon/src/control/glue/ctl_firmware.cpp"
#define SOURCE_MAX 131072
#define NOT_FOUND ((size_t)-1)

static char source[SOURCE_MAX];

/* The code without its comments, so that a word in a comment counts as no read and no call. */
static bool read_code(void)
{
    char raw[SOURCE_MAX];
    FILE *file = fopen(SOURCE_PATH, "r");
    if (file == NULL) {
        return false;
    }
    size_t length = fread(raw, 1, sizeof raw - 1, file);
    fclose(file);
    raw[length] = '\0';
    size_t out = 0;
    for (size_t i = 0; i < length; i++) {
        if (raw[i] == '/' && raw[i + 1] == '/') {
            while (i < length && raw[i] != '\n') {
                i++;
            }
        } else if (raw[i] == '/' && raw[i + 1] == '*') {
            const char *end = strstr(raw + i + 2, "*/");
            i = end == NULL ? length : (size_t)(end - raw) + 1;
            continue;
        }
        if (i < length) {
            source[out++] = raw[i];
        }
    }
    source[out] = '\0';
    return length < sizeof raw - 1;
}

/* The body of the function whose definition starts with the head, from its opening brace to the
 * closing brace at the start of a line; its bounds are written to begin and end. */
static bool find_body(const char *head, size_t *begin, size_t *end)
{
    const char *start = strstr(source, head);
    if (start == NULL) {
        return false;
    }
    const char *open = strstr(start, "\n{\n");
    const char *close = open == NULL ? NULL : strstr(open, "\n}\n");
    if (close == NULL) {
        return false;
    }
    *begin = (size_t)(open - source);
    *end = (size_t)(close - source);
    return true;
}

static bool is_word_character(char c)
{
    return isalnum((unsigned char)c) || c == '_';
}

/* The offset of the first (or last) whole-word occurrence of the word between begin and end. */
static size_t find_word(const char *word, size_t begin, size_t end, bool last)
{
    size_t found = NOT_FOUND;
    size_t length = strlen(word);
    for (size_t i = begin; i + length <= end; i++) {
        bool whole = strncmp(source + i, word, length) == 0
                     && (i == 0 || !is_word_character(source[i - 1]))
                     && !is_word_character(source[i + length]);
        if (whole) {
            found = i;
            if (!last) {
                return found;
            }
        }
    }
    return found;
}

/* Fails when a call of one of the senders comes before the last read of the payload. */
static void check_payload_first(const char *head, const char *const *senders, size_t count)
{
    size_t begin;
    size_t end;
    bool found = find_body(head, &begin, &end);
    check_true(__FILE__, __LINE__, found, head);
    if (!found) {
        return;
    }
    size_t last_read = find_word("payload", begin, end, true);
    check_true(__FILE__, __LINE__, last_read != NOT_FOUND, "the handler reads the payload");
    for (size_t i = 0; i < count; i++) {
        size_t call = find_word(senders[i], begin, end, false);
        if (call != NOT_FOUND && call < last_read) {
            printf("%s:%d: %s: %s is called before the last read of the payload\n", __FILE__,
                   __LINE__, head, senders[i]);
            check_true(__FILE__, __LINE__, false, "the payload is read before anything sends");
        }
    }
}

/* Every call in the glue that can reach the client: directly, through the log, or through a
 * change of the settings, which logs, unsubscribes and subscribes. */
static const char *const senders[] = {
    "ctl_firmware_change_begin", "ctl_firmware_change", "ctl_firmware_change_end",
    "publish_setting",           "publish",             "subscribe_inputs",
    "log_text",                  "log_message",         "decode_heatpump_data",
    "ctl_firmware_frame",        "save_settings",
};

int main(void)
{
    size_t count = sizeof senders / sizeof senders[0];
    bool read = read_code();
    CHECK_TRUE(read);
    if (read) {
        check_payload_first("static void handle_setting(", senders, count);
    }
    return check_result();
}
