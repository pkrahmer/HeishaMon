#include "ctl_format.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static const double POWERS_OF_TEN[CTL_FORMAT_DECIMALS_MAX + 1] = {1.0, 10.0, 100.0, 1000.0};

static bool copy_text(const char *source, char *text, size_t size)
{
    size_t length = 0;
    while (source[length] != '\0') {
        length++;
    }
    if (length + 1 > size) {
        return false;
    }
    for (size_t i = 0; i <= length; i++) {
        text[i] = source[i];
    }
    return true;
}

/* Rounds by comparing the exact fraction rather than adding 0.5, which would itself round up
 * values just below one half. */
static uint64_t round_half_away(double magnitude)
{
    const double whole = floor(magnitude);
    uint64_t units = (uint64_t)whole;
    if (magnitude - whole >= 0.5) {
        units++;
    }
    return units;
}

/* Writes the digits of units from the end of scratch backwards and returns the first one's
 * index; a point goes in after decimals digits. Every number has at least one digit before the
 * point. */
static size_t write_digits(uint64_t units, int decimals, char *scratch, size_t end)
{
    size_t index = end;
    int written = 0;
    while (written <= decimals || units > 0u) {
        if (written == decimals && decimals > 0) {
            scratch[--index] = '.';
        }
        scratch[--index] = (char)('0' + (int)(units % 10u));
        units /= 10u;
        written++;
    }
    return index;
}

static const char *special_text(double value)
{
    if (isnan(value)) {
        return "nan";
    }
    return value > 0.0 ? "inf" : "-inf";
}

bool ctl_format_fixed(double value, int decimals, char *text, size_t size)
{
    char scratch[CTL_FORMAT_TEXT_MAX];
    uint64_t units;
    size_t start;

    if (decimals < 0 || decimals > CTL_FORMAT_DECIMALS_MAX) {
        return false;
    }
    if (!isfinite(value)) {
        return copy_text(special_text(value), text, size);
    }
    if (fabs(value) >= CTL_FORMAT_MAGNITUDE_LIMIT) {
        return false;
    }
    units = round_half_away(fabs(value) * POWERS_OF_TEN[decimals]);
    scratch[CTL_FORMAT_TEXT_MAX - 1] = '\0';
    start = write_digits(units, decimals, scratch, CTL_FORMAT_TEXT_MAX - 1);
    if (value < 0.0 && units > 0u) {
        scratch[--start] = '-';
    }
    return copy_text(&scratch[start], text, size);
}
