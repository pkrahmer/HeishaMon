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

bool ctl_integral_store_parse(const char *text, size_t length, double *value_w)
{
    ctl_payload payload;
    /* The payload parser also takes a JSON object; the file holds a bare number only. */
    if (memchr(text, '{', length) != NULL || !ctl_payload_parse(text, length, &payload) ||
        !isfinite(payload.value)) {
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

double ctl_integral_store_load(ctl_integral_store *store, const char *text, size_t length,
                               double limit_w, double boot_s)
{
    double start_w = 0.0;
    if (text != NULL) {
        start_w = ctl_integral_store_start(text, length, limit_w);
    }
    store->saved_w = start_w;
    store->attempt_s = boot_s;
    return start_w;
}

bool ctl_integral_store_begin(ctl_integral_store *store, double integral_w, double now_s,
                              bool reset)
{
    if (!ctl_integral_store_due(integral_w, store->saved_w, now_s - store->attempt_s, reset)) {
        return false;
    }
    store->attempt_s = now_s;
    return true;
}

void ctl_integral_store_end(ctl_integral_store *store, double integral_w, bool written)
{
    if (written) {
        store->saved_w = integral_w;
    }
}

bool ctl_integral_store_format(double value_w, char *text, size_t size)
{
    if (!isfinite(value_w)) {
        return false;
    }
    return ctl_format_fixed(value_w, FILE_DECIMALS, text, size);
}
