#include "ctl_frame.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const char HEX_DIGITS[] = "0123456789abcdef";

/* Uppercase digits are refused: one spelling per checksum keeps a garbled digit visible. */
static int lowercase_hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

uint8_t ctl_frame_checksum(const char *text, size_t length)
{
    uint8_t sum = 0;
    size_t i;
    for (i = 0; i < length; i++) {
        sum = (uint8_t)(sum ^ (uint8_t)text[i]);
    }
    return sum;
}

bool ctl_frame_add(char *line, size_t size)
{
    size_t length = strlen(line);
    uint8_t sum;
    if (length >= size || size - length < CTL_FRAME_SUFFIX + 1) {
        return false;
    }
    sum = ctl_frame_checksum(line, length);
    line[length] = ' ';
    line[length + 1] = '*';
    line[length + 2] = HEX_DIGITS[sum >> 4];
    line[length + 3] = HEX_DIGITS[sum & 0x0f];
    line[length + 4] = '\0';
    return true;
}

bool ctl_frame_strip(char *line)
{
    size_t length = strlen(line);
    size_t text_length;
    int high;
    int low;
    if (length > 0 && line[length - 1] == '\r') {
        length--;
    }
    if (length < CTL_FRAME_SUFFIX) {
        return false;
    }
    text_length = length - CTL_FRAME_SUFFIX;
    if (line[text_length] != ' ' || line[text_length + 1] != '*') {
        return false;
    }
    high = lowercase_hex_value(line[text_length + 2]);
    low = lowercase_hex_value(line[text_length + 3]);
    if (high < 0 || low < 0 || ctl_frame_checksum(line, text_length) != (uint8_t)(high * 16 + low)) {
        return false;
    }
    line[text_length] = '\0';
    return true;
}
