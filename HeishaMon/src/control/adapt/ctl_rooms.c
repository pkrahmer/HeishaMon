#include "ctl_rooms.h"

#include "ctl_bounds.h"
#include "ctl_payload.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DEFAULT_WEIGHT 1.0
#define DEFAULT_ROOM_TIMEOUT_S 1800.0

void ctl_rooms_config_defaults(ctl_rooms_config *config, size_t count)
{
    size_t i;
    config->count = count;
    for (i = 0; i < CTL_ROOMS_MAX; i++) {
        config->weight[i] = DEFAULT_WEIGHT;
    }
    config->room_timeout_s = DEFAULT_ROOM_TIMEOUT_S;
}

bool ctl_rooms_config_valid(const ctl_rooms_config *config)
{
    double sum = 0.0;
    size_t i;
    if (config->count < 1 || config->count > CTL_ROOMS_MAX) {
        return false;
    }
    for (i = 0; i < config->count; i++) {
        if (!ctl_bounds_accept(CTL_BOUND_WEIGHT, config->weight[i])) {
            return false;
        }
        sum = sum + config->weight[i];
    }
    return sum > 0.0 && isfinite(config->room_timeout_s) && config->room_timeout_s > 0.0;
}

void ctl_rooms_init(ctl_rooms *rooms)
{
    size_t i;
    for (i = 0; i < CTL_ROOMS_MAX; i++) {
        rooms->has_value[i] = false;
        rooms->value_c[i] = 0.0;
        rooms->t[i] = 0.0;
        rooms->has_timestamp[i] = false;
        rooms->timestamp_ms[i] = 0;
    }
}

/* The CTL_ROOMS_MAX test keeps an index inside the arrays even under a count the configuration
 * check would refuse. */
static bool index_valid(const ctl_rooms_config *config, size_t index)
{
    return index < config->count && index < CTL_ROOMS_MAX;
}

/* A thermostat that reconnects may send its last payload again; its timestamp shows that it says
 * nothing new and must not make an old reading look fresh. */
static bool is_replay(const ctl_rooms *rooms, size_t index, const ctl_payload *payload)
{
    return payload->has_timestamp && rooms->has_timestamp[index]
           && payload->timestamp_ms <= rooms->timestamp_ms[index];
}

ctl_room_status ctl_rooms_offer(ctl_rooms *rooms, const ctl_rooms_config *config, size_t index,
                                const char *payload, size_t length, double t)
{
    ctl_payload parsed = {0.0, false, 0};
    if (!index_valid(config, index)) {
        return CTL_ROOM_BAD_INDEX;
    }
    if (!ctl_payload_parse(payload, length, &parsed)) {
        return CTL_ROOM_UNPARSED;
    }
    if (!ctl_bounds_accept(CTL_BOUND_TEMPERATURE, parsed.value) || !isfinite(t)) {
        return CTL_ROOM_IMPLAUSIBLE;
    }
    if (is_replay(rooms, index, &parsed)) {
        return CTL_ROOM_REPLAYED;
    }
    rooms->has_value[index] = true;
    rooms->value_c[index] = parsed.value;
    rooms->t[index] = t;
    rooms->has_timestamp[index] = parsed.has_timestamp;
    rooms->timestamp_ms[index] = parsed.has_timestamp ? parsed.timestamp_ms : 0;
    return CTL_ROOM_ACCEPTED;
}

bool ctl_rooms_is_fresh(const ctl_rooms *rooms, const ctl_rooms_config *config, size_t index,
                        double now)
{
    if (!index_valid(config, index) || !rooms->has_value[index]) {
        return false;
    }
    /* A NaN age compares false, so a NaN now is never fresh. */
    return now - rooms->t[index] <= config->room_timeout_s;
}

void ctl_rooms_average_at(const ctl_rooms *rooms, const ctl_rooms_config *config, double now,
                          ctl_rooms_average *average)
{
    double weighted_sum = 0.0;
    size_t i;
    average->has_average = false;
    average->average_c = 0.0;
    average->fresh_count = 0;
    average->fresh_weight = 0.0;
    average->total_weight = 0.0;
    if (!ctl_rooms_config_valid(config)) {
        return;
    }
    for (i = 0; i < config->count; i++) {
        average->total_weight = average->total_weight + config->weight[i];
        if (ctl_rooms_is_fresh(rooms, config, i, now)) {
            average->fresh_count = average->fresh_count + 1;
            average->fresh_weight = average->fresh_weight + config->weight[i];
            weighted_sum = weighted_sum + config->weight[i] * rooms->value_c[i];
        }
    }
    /* Strictly more than half: two halves of a split house must not both pass for the whole. */
    if (average->fresh_weight > average->total_weight / 2.0) {
        average->has_average = true;
        average->average_c = weighted_sum / average->fresh_weight;
    }
}
