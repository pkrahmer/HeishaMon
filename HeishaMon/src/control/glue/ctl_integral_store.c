#include "ctl_integral_store.h"

#include "../adapt/ctl_payload.h"
#include "../loop/ctl_format.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define FILE_DECIMALS 3

bool ctl_integral_store_due(double integral_w, double saved_w, double since_write_s, bool reset)
{
    if (reset) {
        return true;
    }
    return since_write_s >= CTL_INTEGRAL_STORE_INTERVAL_S &&
           fabs(integral_w - saved_w) >= CTL_INTEGRAL_STORE_MOVE_W;
}

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool ctl_integral_store_parse(const char *text, size_t length, double *value_w)
{
    ctl_payload payload;
    /* A file edited by hand or by a tool may end in a line break. */
    while (length > 0 && is_space(text[length - 1])) {
        length--;
    }
    /* The payload parser also takes a JSON object; the file holds a bare number only. */
    if (length == 0 || memchr(text, '{', length) != NULL ||
        !ctl_payload_parse(text, length, &payload) || !isfinite(payload.value)) {
        return false;
    }
    *value_w = payload.value;
    return true;
}

double ctl_integral_store_start(const char *text, size_t length, double limit_w)
{
    double value_w = 0.0;
    if (!ctl_integral_store_parse(text, length, &value_w)) {
        return 0.0;
    }
    if (value_w > limit_w) {
        return limit_w;
    }
    if (value_w < -limit_w) {
        return -limit_w;
    }
    return value_w;
}

bool ctl_integral_store_format(double value_w, char *text, size_t size)
{
    if (!isfinite(value_w)) {
        return false;
    }
    return ctl_format_fixed(value_w, FILE_DECIMALS, text, size);
}
