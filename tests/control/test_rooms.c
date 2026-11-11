#include "check.h"

#include "adapt/ctl_rooms.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static ctl_room_status offer_text(ctl_rooms *rooms, const ctl_rooms_config *config, size_t index,
                                  const char *text, double t)
{
    return ctl_rooms_offer(rooms, config, index, text, strlen(text), t);
}

/* Compares every field of every slot, so that a change to a room no row names is caught too. */
static void check_rooms_equal(const char *file, int line, const ctl_rooms *actual,
                              const ctl_rooms *expected)
{
    size_t i;
    for (i = 0; i < CTL_ROOMS_MAX; i++) {
        check_true(file, line, actual->has_value[i] == expected->has_value[i], "has_value");
        check_double(file, line, actual->value_c[i], expected->value_c[i]);
        check_double(file, line, actual->t[i], expected->t[i]);
        check_true(file, line, actual->has_timestamp[i] == expected->has_timestamp[i],
                   "has_timestamp");
        check_true(file, line, actual->timestamp_ms[i] == expected->timestamp_ms[i],
                   "timestamp_ms");
    }
}

static void test_config(void)
{
    static const ctl_rooms_config zero_config;
    ctl_rooms_config defaults = zero_config;
    size_t i;
    ctl_rooms_config_defaults(&defaults, 4);
    CHECK_UINT((uint32_t)defaults.count, 4u);
    for (i = 0; i < 4; i++) {
        CHECK_DOUBLE(defaults.weight[i], 1.0);
    }
    CHECK_DOUBLE(defaults.room_timeout_s, 1800.0);
    CHECK_TRUE(ctl_rooms_config_valid(&defaults));

    {
        enum { COUNT, WEIGHT_1, ALL_WEIGHTS, TIMEOUT };
        static const struct {
            int field;
            double value;
            bool valid;
        } rows[] = {
            {COUNT, 0.0, false},
            {COUNT, 17.0, false},
            {WEIGHT_1, -1.0, false},
            {WEIGHT_1, 1000.001, false},
            {ALL_WEIGHTS, 0.0, false},
            {TIMEOUT, 0.0, false},
            {TIMEOUT, -1.0, false},
            {TIMEOUT, NAN, false},
            {COUNT, 16.0, true},
            {WEIGHT_1, 1000.0, true},
            {WEIGHT_1, 0.0, true},
        };
        for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
            ctl_rooms_config config = zero_config;
            size_t j;
            ctl_rooms_config_defaults(&config, 4);
            switch (rows[i].field) {
            case COUNT:
                config.count = (size_t)rows[i].value;
                break;
            case WEIGHT_1:
                config.weight[1] = rows[i].value;
                break;
            case ALL_WEIGHTS:
                for (j = 0; j < config.count; j++) {
                    config.weight[j] = rows[i].value;
                }
                break;
            default:
                config.room_timeout_s = rows[i].value;
                break;
            }
            /* A count of 16 needs the slots beyond 4 to hold accepted weights. */
            for (j = 4; j < CTL_ROOMS_MAX; j++) {
                config.weight[j] = 1.0;
            }
            CHECK_TRUE(ctl_rooms_config_valid(&config) == rows[i].valid);
        }
    }
}

static void test_offer(void)
{
    static const struct {
        size_t index;
        const char *text;
        double t;
        ctl_room_status status;
        double value_c;
        bool has_timestamp;
        int64_t timestamp_ms;
    } rows[] = {
        {4, "21.0", 10.0, CTL_ROOM_BAD_INDEX, 0.0, false, 0},
        {0, "abc", 10.0, CTL_ROOM_UNPARSED, 0.0, false, 0},
        {0, "61", 10.0, CTL_ROOM_IMPLAUSIBLE, 0.0, false, 0},
        {0, "{\"v\":-60.5}", 10.0, CTL_ROOM_IMPLAUSIBLE, 0.0, false, 0},
        {0, "21.0", NAN, CTL_ROOM_IMPLAUSIBLE, 0.0, false, 0},
        {0, "{\"ts\":100,\"v\":21.0}", 20.0, CTL_ROOM_ACCEPTED, 21.0, true, 100},
        {0, "{\"ts\":100,\"v\":22.0}", 30.0, CTL_ROOM_REPLAYED, 0.0, false, 0},
        {0, "{\"ts\":99,\"v\":22.0}", 40.0, CTL_ROOM_REPLAYED, 0.0, false, 0},
        {0, "{\"ts\":101,\"v\":22.0}", 50.0, CTL_ROOM_ACCEPTED, 22.0, true, 101},
        {0, "23.0", 60.0, CTL_ROOM_ACCEPTED, 23.0, false, 0},
        {0, "{\"ts\":50,\"v\":24.0}", 70.0, CTL_ROOM_ACCEPTED, 24.0, true, 50},
    };
    ctl_rooms_config config;
    ctl_rooms rooms;
    size_t i;
    ctl_rooms_config_defaults(&config, 4);
    ctl_rooms_init(&rooms);
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        ctl_rooms expected = rooms;
        ctl_room_status status = offer_text(&rooms, &config, rows[i].index, rows[i].text, rows[i].t);
        CHECK_UINT((uint32_t)status, (uint32_t)rows[i].status);
        if (rows[i].status == CTL_ROOM_ACCEPTED) {
            size_t k = rows[i].index;
            expected.has_value[k] = true;
            expected.value_c[k] = rows[i].value_c;
            expected.t[k] = rows[i].t;
            expected.has_timestamp[k] = rows[i].has_timestamp;
            expected.timestamp_ms[k] = rows[i].timestamp_ms;
        }
        check_rooms_equal(__FILE__, __LINE__, &rooms, &expected);
    }
}

static void test_freshness_at_the_limit(void)
{
    const struct {
        double now;
        bool fresh;
    } rows[] = {
        {2800.0, true},
        {nextafter(2800.0, INFINITY), false},
        {500.0, true},
        {NAN, false},
    };
    ctl_rooms_config config;
    ctl_rooms rooms;
    size_t i;
    ctl_rooms_config_defaults(&config, 4);
    ctl_rooms_init(&rooms);
    CHECK_UINT((uint32_t)offer_text(&rooms, &config, 0, "21.0", 1000.0),
               (uint32_t)CTL_ROOM_ACCEPTED);
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        CHECK_TRUE(ctl_rooms_is_fresh(&rooms, &config, 0, rows[i].now) == rows[i].fresh);
    }
    CHECK_TRUE(!ctl_rooms_is_fresh(&rooms, &config, 1, 1000.0));
}

static const double ROOM_VALUES[4] = {21.0, 22.0, 23.0, 24.0};
static const char *const ROOM_TEXTS[4] = {"21.0", "22.0", "23.0", "24.0"};

static void offer_room(ctl_rooms *rooms, const ctl_rooms_config *config, size_t index, double t)
{
    CHECK_UINT((uint32_t)offer_text(rooms, config, index, ROOM_TEXTS[index], t),
               (uint32_t)CTL_ROOM_ACCEPTED);
}

static void offer_all(ctl_rooms *rooms, const ctl_rooms_config *config, double t)
{
    size_t i;
    for (i = 0; i < 4; i++) {
        offer_room(rooms, config, i, t);
    }
}

static void test_equal_weights(void)
{
    ctl_rooms_config config;
    ctl_rooms rooms;
    ctl_rooms_average average;
    ctl_rooms_config_defaults(&config, 4);
    ctl_rooms_init(&rooms);
    offer_all(&rooms, &config, 0.0);
    ctl_rooms_average_at(&rooms, &config, 60.0, &average);
    CHECK_TRUE(average.has_average);
    CHECK_DOUBLE(average.average_c, 22.5);
    CHECK_UINT((uint32_t)average.fresh_count, 4u);
    CHECK_DOUBLE(average.fresh_weight, 4.0);
    CHECK_DOUBLE(average.total_weight, 4.0);
}

static void test_weights(void)
{
    ctl_rooms_config config;
    ctl_rooms rooms;
    ctl_rooms_average average;
    ctl_rooms_config_defaults(&config, 4);
    config.weight[0] = 1.0;
    config.weight[1] = 2.0;
    config.weight[2] = 1.0;
    config.weight[3] = 0.0;
    ctl_rooms_init(&rooms);
    offer_all(&rooms, &config, 0.0);
    ctl_rooms_average_at(&rooms, &config, 60.0, &average);
    CHECK_TRUE(average.has_average);
    CHECK_DOUBLE(average.average_c, (1.0 * 21.0 + 2.0 * 22.0 + 1.0 * 23.0) / 4.0);
    CHECK_UINT((uint32_t)average.fresh_count, 4u);
    CHECK_DOUBLE(average.fresh_weight, 4.0);
    CHECK_DOUBLE(average.total_weight, 4.0);
}

static void test_half_rule(void)
{
    /* A stale room is offered at 0.0, a fresh one at 1800.0; the average is asked at 1900.0. */
    static const struct {
        double weight[4];
        bool fresh[4];
        bool has_average;
        double average_c;
        size_t fresh_count;
        double fresh_weight;
        double total_weight;
    } rows[] = {
        {{1.0, 1.0, 1.0, 1.0}, {true, true, true, false}, true,
         (1.0 * 21.0 + 1.0 * 22.0 + 1.0 * 23.0) / 3.0, 3, 3.0, 4.0},
        {{1.0, 1.0, 1.0, 1.0}, {true, true, false, false}, false, 0.0, 2, 2.0, 4.0},
        {{3.0, 1.0, 1.0, 1.0}, {true, false, false, false}, false, 0.0, 1, 3.0, 6.0},
        {{3.1, 1.0, 1.0, 1.0}, {true, false, false, false}, true, (3.1 * 21.0) / 3.1, 1, 3.1,
         6.1},
    };
    size_t i;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        ctl_rooms_config config;
        ctl_rooms rooms;
        ctl_rooms_average average;
        size_t k;
        ctl_rooms_config_defaults(&config, 4);
        ctl_rooms_init(&rooms);
        for (k = 0; k < 4; k++) {
            config.weight[k] = rows[i].weight[k];
        }
        for (k = 0; k < 4; k++) {
            offer_room(&rooms, &config, k, rows[i].fresh[k] ? 1800.0 : 0.0);
        }
        ctl_rooms_average_at(&rooms, &config, 1900.0, &average);
        CHECK_TRUE(average.has_average == rows[i].has_average);
        CHECK_DOUBLE(average.average_c, rows[i].average_c);
        CHECK_UINT((uint32_t)average.fresh_count, (uint32_t)rows[i].fresh_count);
        CHECK_DOUBLE(average.fresh_weight, rows[i].fresh_weight);
        CHECK_DOUBLE(average.total_weight, rows[i].total_weight);
    }
}

static void test_room_leaving_and_returning(void)
{
    ctl_rooms_config config;
    ctl_rooms rooms;
    ctl_rooms_average average;
    ctl_rooms_config_defaults(&config, 4);
    ctl_rooms_init(&rooms);
    offer_all(&rooms, &config, 0.0);
    offer_room(&rooms, &config, 2, 1000.0);

    ctl_rooms_average_at(&rooms, &config, 2000.0, &average);
    CHECK_TRUE(!average.has_average);
    CHECK_DOUBLE(average.average_c, 0.0);
    CHECK_UINT((uint32_t)average.fresh_count, 1u);
    CHECK_DOUBLE(average.fresh_weight, 1.0);

    offer_room(&rooms, &config, 0, 2100.0);
    offer_room(&rooms, &config, 1, 2100.0);
    offer_room(&rooms, &config, 3, 2100.0);
    ctl_rooms_average_at(&rooms, &config, 2100.0, &average);
    CHECK_TRUE(average.has_average);
    CHECK_UINT((uint32_t)average.fresh_count, 4u);
    CHECK_DOUBLE(average.fresh_weight, 4.0);
    CHECK_DOUBLE(average.average_c,
                 (1.0 * ROOM_VALUES[0] + 1.0 * ROOM_VALUES[1] + 1.0 * ROOM_VALUES[2]
                  + 1.0 * ROOM_VALUES[3])
                     / 4.0);
}

int main(void)
{
    test_config();
    test_offer();
    test_freshness_at_the_limit();
    test_equal_weights();
    test_weights();
    test_half_rule();
    test_room_leaving_and_returning();
    return check_result();
}
