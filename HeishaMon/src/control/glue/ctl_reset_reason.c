#include "ctl_reset_reason.h"

#include "../loop/ctl_text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ESP8266_EXCEPTION 2u
#define HEX_DIGITS 8

/* Indexed by the SDK's enum rst_reason; the exception is written apart, with its fields. */
static const char *const ESP8266_NAMES[] = {
    "power on",         "hardware watchdog", "exception",      "software watchdog",
    "software restart", "deep sleep wake",   "external reset",
};

/* Indexed by esp_reset_reason_t, from ESP_RST_UNKNOWN to ESP_RST_CPU_LOCKUP. */
static const char *const ESP32_NAMES[] = {
    "unknown",         "power on",           "external reset", "software restart",
    "exception",       "interrupt watchdog", "task watchdog",  "other watchdog",
    "deep sleep wake", "brownout",           "sdio reset",     "usb reset",
    "jtag reset",      "efuse error",        "power glitch",   "cpu lockup",
};

static void put_decimal(ctl_text *writer, uint32_t value)
{
    ctl_text_put_number(writer, (double)value, 0);
}

static void put_hex(ctl_text *writer, uint32_t value)
{
    static const char digits[] = "0123456789abcdef";
    char text[HEX_DIGITS + 1];
    int i;
    for (i = HEX_DIGITS - 1; i >= 0; i--) {
        text[i] = digits[value & 0xfu];
        value >>= 4;
    }
    text[HEX_DIGITS] = '\0';
    ctl_text_put(writer, "0x");
    ctl_text_put(writer, text);
}

static void put_field(ctl_text *writer, const char *name, uint32_t value)
{
    ctl_text_put(writer, " ");
    ctl_text_put(writer, name);
    ctl_text_put(writer, "=");
    put_hex(writer, value);
}

static void put_exception(ctl_text *writer, const ctl_reset_info *info)
{
    ctl_text_put(writer, "exception ");
    put_decimal(writer, info->exccause);
    put_field(writer, "epc1", info->epc1);
    put_field(writer, "epc2", info->epc2);
    put_field(writer, "epc3", info->epc3);
    put_field(writer, "excvaddr", info->excvaddr);
    put_field(writer, "depc", info->depc);
}

static void put_name(ctl_text *writer, const char *const *names, size_t count, uint32_t reason)
{
    if (reason < count) {
        ctl_text_put(writer, names[reason]);
        return;
    }
    ctl_text_put(writer, "reason ");
    put_decimal(writer, reason);
}

/* The text is written into a buffer of the longest size first and copied out only when it fits
 * the caller's, so that a refusal writes nothing. */
static bool deliver(const ctl_text *writer, char *text, size_t size)
{
    if (!writer->fits || writer->length >= size) {
        return false;
    }
    memcpy(text, writer->text, writer->length + 1);
    return true;
}

bool ctl_reset_reason_esp8266(const ctl_reset_info *info, char *text, size_t size)
{
    char buffer[CTL_RESET_REASON_TEXT_MAX];
    ctl_text writer;
    ctl_text_start(&writer, buffer, sizeof buffer);
    if (info->reason == ESP8266_EXCEPTION) {
        put_exception(&writer, info);
    } else {
        put_name(&writer, ESP8266_NAMES, sizeof ESP8266_NAMES / sizeof ESP8266_NAMES[0],
                 info->reason);
    }
    return deliver(&writer, text, size);
}

bool ctl_reset_reason_esp32(uint32_t reason, char *text, size_t size)
{
    char buffer[CTL_RESET_REASON_TEXT_MAX];
    ctl_text writer;
    ctl_text_start(&writer, buffer, sizeof buffer);
    put_name(&writer, ESP32_NAMES, sizeof ESP32_NAMES / sizeof ESP32_NAMES[0], reason);
    return deliver(&writer, text, size);
}

bool ctl_reset_latch_due(const ctl_reset_latch *latch)
{
    return !latch->published;
}

void ctl_reset_latch_record(ctl_reset_latch *latch, bool taken)
{
    if (taken) {
        latch->published = true;
    }
}
