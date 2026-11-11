/*
 * The payload parser of the adapters: it turns the text of an MQTT payload into a number and,
 * where the payload carries one, a timestamp. The sources send two shapes, a bare number (as
 * HeishaMon publishes its values) and a flat JSON object with a "v" or "value" member and an
 * optional "ts" (as the CCU3 publishes a room temperature). The parser reads exactly these
 * shapes and refuses everything else, so that a payload a source never sends cannot slip a value
 * past the adapter. It allocates nothing and reads nothing beyond the length it is given.
 */
#ifndef CTL_PAYLOAD_H
#define CTL_PAYLOAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CTL_PAYLOAD_MAX 256 /* the longest payload parsed; a longer one is refused */

typedef struct {
    double value;
    bool has_timestamp;
    int64_t timestamp_ms; /* the payload's "ts", when it has one */
} ctl_payload;

/* Parses the length bytes of text, which need no terminating zero, and returns true with the
 * value, and the timestamp if there is one, in *payload. Refuses, returning false and leaving
 * *payload as it was, a length above CTL_PAYLOAD_MAX, any text that is neither a bare number nor
 * an object of the accepted form, an object without exactly one value member or with a key
 * twice, a timestamp that is not 1 to 18 digits, and a number that is not finite as a double. */
bool ctl_payload_parse(const char *text, size_t length, ctl_payload *payload);

#endif
