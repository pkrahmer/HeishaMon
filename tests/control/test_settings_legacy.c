#include "check.h"

#include "glue/ctl_settings.h"
#include "glue/ctl_settings_file.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* A settings file as a board wrote it while it still had the pump topics. */
static const char LEGACY_FILE[] = "{\"enabled\":\"0\",\"heat_topic\":\"a/b\","
                                  "\"outdoor_topic\":\"c/d\",\"hysteresis_wh\":\"1200\"}\n";

static bool load(ctl_settings *settings, const char *text)
{
    ctl_settings_file file;
    size_t i;
    ctl_settings_defaults(settings);
    ctl_settings_file_start(&file);
    for (i = 0; text[i] != '\0'; i++) {
        ctl_settings_file_feed(&file, settings, text[i]);
    }
    return ctl_settings_file_complete(&file);
}

static void write_all(const ctl_settings *settings, char *text, size_t size)
{
    char piece[CTL_SETTINGS_FILE_PIECE_MAX];
    size_t length = 0;
    size_t i;
    text[0] = '\0';
    for (i = 0; i < ctl_settings_file_pieces(settings); i++) {
        CHECK_TRUE(ctl_settings_file_piece(settings, i, piece, sizeof piece));
        CHECK_TRUE(length + strlen(piece) < size);
        if (length + strlen(piece) < size) {
            strcpy(text + length, piece);
            length += strlen(piece);
        }
    }
}

static void test_legacy_file_loads(void)
{
    ctl_settings settings;
    char value[CTL_SETTINGS_VALUE_MAX];
    CHECK_TRUE(load(&settings, LEGACY_FILE));
    CHECK_TRUE(ctl_settings_valid(&settings));
    CHECK_TRUE(ctl_settings_get(&settings, "hysteresis_wh", value, sizeof value));
    CHECK_TEXT(value, "1200");
    CHECK_TRUE(!ctl_settings_get(&settings, "heat_topic", value, sizeof value));
    CHECK_TRUE(!ctl_settings_get(&settings, "outdoor_topic", value, sizeof value));
}

static void test_written_file_drops_them(void)
{
    ctl_settings settings;
    static char text[CTL_SETTINGS_NAMES_MAX * CTL_SETTINGS_FILE_PIECE_MAX];
    CHECK_TRUE(load(&settings, LEGACY_FILE));
    write_all(&settings, text, sizeof text);
    CHECK_TRUE(strstr(text, "\"hysteresis_wh\":\"1200\"") != NULL);
    CHECK_TRUE(strstr(text, "heat_topic") == NULL);
    CHECK_TRUE(strstr(text, "outdoor_topic") == NULL);
}

int main(void)
{
    test_legacy_file_loads();
    test_written_file_drops_them();
    return check_result();
}
