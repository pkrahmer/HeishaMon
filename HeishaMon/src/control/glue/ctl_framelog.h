/*
 * The frame log: rebuilds a frame the pump sent to another HeishaMon from that board's log lines,
 * so that a board without a pump of its own can follow a real one through MQTT alone. HeishaMon
 * with hex dumps on logs every received frame as a "Received <n> bytes data" line followed by
 * "data: " lines of hex bytes, and every frame it sends as a "sent bytes: <n> including checksum
 * value: <c>" line followed by the "data: " lines of all bytes but the checksum, which the header
 * gives in decimal; the lines arrive one message each and are fed here in their order. It is
 * pure: it reads no topic and decodes no value.
 */
#ifndef CTL_FRAMELOG_H
#define CTL_FRAMELOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CTL_FRAMELOG_MAX 256 /* the longest frame, in bytes */
#define CTL_FRAMELOG_MAIN_LENGTH 203 /* a main data frame, as upstream's decoder reads it */
#define CTL_FRAMELOG_PCB_LENGTH 20   /* an optional PCB frame, as upstream sends it */

typedef struct {
    bool collecting;
    bool sent;        /* the frame in progress, or the last one, is one the board sent */
    size_t expected;  /* the bytes its data lines give: the <n> of the header, less a sent checksum */
    uint8_t checksum; /* the <c> of a sent frame's header, appended once its data lines are read */
    size_t length;
    uint8_t frame[CTL_FRAMELOG_MAX];
} ctl_framelog;

typedef enum {
    CTL_FRAMELOG_NOTHING,
    CTL_FRAMELOG_FRAME,
    CTL_FRAMELOG_DROPPED,
    CTL_FRAMELOG_SENT
} ctl_framelog_result;

/* Starts with no frame in progress. Refuses nothing. */
void ctl_framelog_init(ctl_framelog *log);

/* Reads one log line of length characters (no terminating zero needed); only the text after the
 * first "): " counts. Returns FRAME on the data line that completes a received frame whose
 * checksum holds and SENT on the one that completes a sent frame whose checksum holds, the
 * header's checksum appended as its last byte; the frame is in log->frame and log->length until
 * the next line that starts one. Returns DROPPED when a frame in progress ends without one: a bad
 * checksum, more bytes than announced, a malformed byte, a new "Received" line or any line with
 * "sent bytes: " in it; NOTHING for every other line, a NULL line and a line without "): "
 * included. A "Received" line with a count of 0 or above CTL_FRAMELOG_MAX starts nothing, nor does
 * a sent header with a count below 2 or above CTL_FRAMELOG_MAX or a checksum above 255. */
ctl_framelog_result ctl_framelog_line(ctl_framelog *log, const char *line, size_t length);

/* Returns true when the sum of all length bytes, the checksum included, is 0 modulo 256; false
 * for a NULL frame. */
bool ctl_framelog_checksum_ok(const uint8_t *frame, size_t length);

/* Returns true when the frame is a main data frame that upstream's decoder may read:
 * CTL_FRAMELOG_MAIN_LENGTH bytes, header 0x71 and type 0x10 in byte 3. False for every other
 * frame (an optional PCB answer, the extra data block) and for a NULL frame. */
bool ctl_framelog_is_main_data(const uint8_t *frame, size_t length);

/* Returns true when the frame is an optional PCB frame as a board sends it:
 * CTL_FRAMELOG_PCB_LENGTH bytes, header 0xF1 and type 0x50 in byte 3. False for every other frame
 * (a query, a command) and for a NULL frame. */
bool ctl_framelog_is_optional_pcb(const uint8_t *frame, size_t length);

#endif
