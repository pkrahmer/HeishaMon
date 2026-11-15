/*
 * The reset reason as one line of text: a board without a serial cable loses the crash message
 * of a restart, but the chip keeps why it last reset, and published once after the next boot that
 * line tells the cause. The glue reads the fields from the framework and passes them as numbers,
 * so that the text is built and tested on the host.
 */
#ifndef CTL_RESET_REASON_H
#define CTL_RESET_REASON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Holds the longest text with its terminating zero: an exception with a ten-digit cause and every
 * address at its full eight digits. */
#define CTL_RESET_REASON_TEXT_MAX 105

/* The ESP8266's reset information, as the SDK's struct rst_info holds it. */
typedef struct {
    uint32_t reason;
    uint32_t exccause;
    uint32_t epc1;
    uint32_t epc2;
    uint32_t epc3;
    uint32_t excvaddr;
    uint32_t depc;
} ctl_reset_info;

/* Writes the ESP8266's reset reason, such as "power on", or for an exception (reason 2) its cause
 * and its five addresses in hexadecimal with eight digits; an unknown reason is "reason <n>".
 * Returns true. Refuses, returning false and writing nothing, a size that cannot hold the text
 * with its terminating zero. */
bool ctl_reset_reason_esp8266(const ctl_reset_info *info, char *text, size_t size);

/* Writes the ESP32's reset reason, numbered as esp_reset_reason_t numbers it, in the same plain
 * words; the ESP32 keeps no exception fields, so a panic is "exception" alone. An unknown reason
 * is "reason <n>". Returns true; refuses as ctl_reset_reason_esp8266 does. */
bool ctl_reset_reason_esp32(uint32_t reason, char *text, size_t size);

/* Whether the reset reason still waits to be published in this boot. The message is retained, so
 * once per boot is enough and a later connect would only repeat it; but a message the client did
 * not take never reached the broker, so it stays due until a publish is taken. */
typedef struct {
    bool published;
} ctl_reset_latch;

#define CTL_RESET_LATCH_INIT {false}

/* Returns true while no publish of the reset reason has been taken in this boot. Refuses nothing. */
bool ctl_reset_latch_due(const ctl_reset_latch *latch);

/* Records the outcome of a publish: a taken one ends the latch for the boot, a refused one leaves it
 * due. Returns nothing and refuses nothing; once ended, the latch stays ended. */
void ctl_reset_latch_record(ctl_reset_latch *latch, bool taken);

#endif
