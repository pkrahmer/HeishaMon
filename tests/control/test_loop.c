#include "check.h"

#include "core/ctl_controller.h"
#include "core/ctl_relay.h"
#include "core/ctl_signals.h"
#include "core/ctl_types.h"
#include "loop/ctl_format.h"
#include "loop/ctl_loop.h"
#include "route/ctl_router.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ROOMS 4
#define HEATING_AND_NONE \
    ((uint8_t)(CTL_DEMAND_BIT(CTL_DEMAND_HEATING) | CTL_DEMAND_BIT(CTL_DEMAND_NONE)))
#define NO_ROOM 0
#define NUMBER_SIZE 32
#define THERMOSTAT "SetExternalThermostat1State"
#define HEAT_COOL "SetHeatCoolMode"

#define CHECK_STATUS(actual, expected) CHECK_UINT((uint32_t)(actual), (uint32_t)(expected))

static void start(ctl_loop *loop, ctl_loop_config *config)
{
    ctl_loop_config_defaults(config, ROOMS);
    ctl_loop_init(loop, config, 0.0);
}

static ctl_input_status give(ctl_loop *loop, const ctl_loop_config *config, ctl_input input,
                             size_t room, const char *text, double now)
{
    return ctl_loop_input(loop, config, input, room, text, strlen(text), now);
}

static ctl_input_status give_number(ctl_loop *loop, const ctl_loop_config *config,
                                    ctl_input input, size_t room, double value, double now)
{
    char text[NUMBER_SIZE];
    CHECK_TRUE(ctl_format_fixed(value, 2, text, sizeof text));
    return give(loop, config, input, room, text, now);
}

/* The house as the pump and the rooms report it: every room at the same temperature. */
typedef struct {
    double room_c;
    double outdoor_c;
    double heat_w;
} house;

static void report_house(ctl_loop *loop, const ctl_loop_config *config, const house *report,
                         double now)
{
    size_t room;
    for (room = 0; room < ROOMS; room++) {
        CHECK_STATUS(give_number(loop, config, CTL_INPUT_ROOM, room, report->room_c, now),
                     CTL_INPUT_TAKEN);
    }
    CHECK_STATUS(give_number(loop, config, CTL_INPUT_OUTDOOR, NO_ROOM, report->outdoor_c, now),
                 CTL_INPUT_TAKEN);
    CHECK_STATUS(give_number(loop, config, CTL_INPUT_HEAT, NO_ROOM, report->heat_w, now),
                 CTL_INPUT_TAKEN);
}

/* ---- The configuration ---- */

typedef enum { KEEP, ZERO_HYSTERESIS, NO_ROOMS, ELEVEN_RETRIES, ALL_THREE_BITS, FOURTH_BIT } config_change;

typedef struct {
    config_change change;
    bool valid;
} config_row;

static void apply_change(ctl_loop_config *config, config_change change)
{
    switch (change) {
    case ZERO_HYSTERESIS:
        config->core.hysteresis_wh = 0.0;
        break;
    case NO_ROOMS:
        config->rooms.count = 0;
        break;
    case ELEVEN_RETRIES:
        config->router.max_retries = 11;
        break;
    case ALL_THREE_BITS:
        config->allowed = 0x07u;
        break;
    case FOURTH_BIT:
        config->allowed = 0x08u;
        break;
    default:
        break;
    }
}

static void test_configuration(void)
{
    static const config_row rows[] = {
        {KEEP, true},
        {ZERO_HYSTERESIS, false},
        {NO_ROOMS, false},
        {ELEVEN_RETRIES, false},
        {ALL_THREE_BITS, true},
        {FOURTH_BIT, false},
    };
    size_t i;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        ctl_loop_config config;
        ctl_loop_config_defaults(&config, ROOMS);
        apply_change(&config, rows[i].change);
        CHECK_TRUE(ctl_loop_config_valid(&config) == rows[i].valid);
    }
}

static void test_configuration_defaults(void)
{
    ctl_loop_config config;
    ctl_loop_config_defaults(&config, ROOMS);
    CHECK_UINT(config.allowed, HEATING_AND_NONE);
    CHECK_TRUE(!config.assume_landed);
    CHECK_UINT((uint32_t)config.rooms.count, ROOMS);
    CHECK_DOUBLE(config.core.hysteresis_wh, 1500.0);
    CHECK_UINT((uint32_t)config.router.max_retries, 3u);
}

/* ---- The inputs ---- */

/* One input on the same loop, in order: what it gives and, when signal is a member, the value
 * and instant the core then holds for that signal. */
typedef struct {
    ctl_input input;
    size_t room;
    const char *text;
    double now;
    ctl_input_status status;
    ctl_signal signal;
    double value;
} input_row;

static void test_inputs(void)
{
    const input_row rows[] = {
        {CTL_INPUT_ROOM, 0, "21.0", 1.0, CTL_INPUT_TAKEN, CTL_SIGNAL_COUNT, 0.0},
        {CTL_INPUT_ROOM, 4, "21.0", 1.0, CTL_INPUT_BAD, CTL_SIGNAL_COUNT, 0.0},
        {CTL_INPUT_ROOM, 1, "warm", 1.0, CTL_INPUT_REFUSED, CTL_SIGNAL_COUNT, 0.0},
        {CTL_INPUT_OUTDOOR, NO_ROOM, "5", 2.0, CTL_INPUT_TAKEN, CTL_SIGNAL_T_OUTDOOR, 5.0},
        {CTL_INPUT_OUTDOOR, NO_ROOM, "99", 3.0, CTL_INPUT_REFUSED, CTL_SIGNAL_T_OUTDOOR, 5.0},
        {CTL_INPUT_HEAT, NO_ROOM, "3200", 4.0, CTL_INPUT_TAKEN, CTL_SIGNAL_P_PRODUCED, 3200.0},
        {CTL_INPUT_VALVE, NO_ROOM, "0", 5.0, CTL_INPUT_NO_READING, CTL_SIGNAL_P_PRODUCED, 3200.0},
        {CTL_INPUT_VALVE, NO_ROOM, "2", 5.0, CTL_INPUT_REFUSED, CTL_SIGNAL_P_PRODUCED, 3200.0},
        {CTL_INPUT_VALVE, NO_ROOM, "1", 6.0, CTL_INPUT_TAKEN, CTL_SIGNAL_P_PRODUCED, 0.0},
        {CTL_INPUT_DEFROST, NO_ROOM, "0", 7.0, CTL_INPUT_NO_READING, CTL_SIGNAL_P_PRODUCED, 0.0},
        {CTL_INPUT_DEFROST, NO_ROOM, "x", 7.0, CTL_INPUT_REFUSED, CTL_SIGNAL_P_PRODUCED, 0.0},
        {CTL_INPUT_PV, NO_ROOM, "1000", 8.0, CTL_INPUT_TAKEN, CTL_SIGNAL_P_SOLAR, 1000.0 * 0.02},
        {CTL_INPUT_PV, NO_ROOM, "-1", 9.0, CTL_INPUT_REFUSED, CTL_SIGNAL_P_SOLAR, 1000.0 * 0.02},
        {(ctl_input)9, NO_ROOM, "1", 9.0, CTL_INPUT_BAD, CTL_SIGNAL_COUNT, 0.0},
        {CTL_INPUT_OUTDOOR, NO_ROOM, "6", NAN, CTL_INPUT_BAD, CTL_SIGNAL_T_OUTDOOR, 5.0},
        {CTL_INPUT_OUTDOOR, NO_ROOM, "6", INFINITY, CTL_INPUT_BAD, CTL_SIGNAL_T_OUTDOOR, 5.0},
        {CTL_INPUT_OUTDOOR, NO_ROOM, "6", -INFINITY, CTL_INPUT_BAD, CTL_SIGNAL_T_OUTDOOR, 5.0},
    };
    ctl_loop_config config;
    ctl_loop loop;
    size_t i;
    start(&loop, &config);
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        const input_row *row = &rows[i];
        CHECK_STATUS(give(&loop, &config, row->input, row->room, row->text, row->now),
                     row->status);
        if (row->signal != CTL_SIGNAL_COUNT) {
            CHECK_TRUE(loop.state.signals.observed[row->signal]);
            CHECK_DOUBLE(loop.state.signals.value[row->signal], row->value);
        }
    }
    /* A room waits for the decision tick; the reading of an adapter carries its own instant. */
    CHECK_TRUE(!loop.state.signals.observed[CTL_SIGNAL_T_INDOOR]);
    CHECK_DOUBLE(loop.state.signals.t[CTL_SIGNAL_T_OUTDOOR], 2.0);
    CHECK_DOUBLE(loop.state.signals.t[CTL_SIGNAL_P_SOLAR], 8.0);
}

/* ---- The ticks ---- */

typedef struct {
    double now;
    bool decided;
    double t_last;
} tick_row;

static void test_ticks(void)
{
    static const tick_row rows[] = {
        {9.0, false, 0.0},
        {10.0, false, 10.0},
        {60.0, true, 60.0},
        {61.0, false, 60.0},
        {120.0, true, 120.0},
    };
    ctl_loop_config config;
    ctl_loop loop;
    ctl_loop_record record;
    size_t i;
    start(&loop, &config);
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        CHECK_TRUE(ctl_loop_tick(&loop, &config, rows[i].now, &record) == rows[i].decided);
        CHECK_DOUBLE(loop.state.t_last, rows[i].t_last);
    }
}

static void test_tick_refuses_not_finite(void)
{
    const double instants[] = {NAN, INFINITY, -INFINITY};
    size_t i;
    for (i = 0; i < sizeof instants / sizeof instants[0]; i++) {
        ctl_loop_config config;
        ctl_loop loop;
        ctl_loop before;
        ctl_loop_record record;
        start(&loop, &config);
        CHECK_TRUE(ctl_loop_tick(&loop, &config, 60.0, &record));
        memcpy(&before, &loop, sizeof loop);
        CHECK_TRUE(!ctl_loop_tick(&loop, &config, instants[i], &record));
        CHECK_TRUE(memcmp(&before, &loop, sizeof loop) == 0);
    }
}

/* ---- The decisions ---- */

static void test_decisions_equal_the_core(void)
{
    static const char *const rooms[ROOMS] = {"21.0", "22.0", "23.0", "24.0"};
    ctl_loop_config config;
    ctl_loop loop;
    ctl_state direct;
    ctl_loop_record record;
    size_t room;
    int step;
    int decisions = 0;

    start(&loop, &config);
    for (room = 0; room < ROOMS; room++) {
        CHECK_STATUS(give(&loop, &config, CTL_INPUT_ROOM, room, rooms[room], 0.0),
                     CTL_INPUT_TAKEN);
    }
    CHECK_STATUS(give(&loop, &config, CTL_INPUT_OUTDOOR, NO_ROOM, "5", 0.0), CTL_INPUT_TAKEN);
    CHECK_STATUS(give(&loop, &config, CTL_INPUT_HEAT, NO_ROOM, "0", 0.0), CTL_INPUT_TAKEN);
    CHECK_STATUS(give(&loop, &config, CTL_INPUT_VALVE, NO_ROOM, "0", 0.0), CTL_INPUT_NO_READING);

    ctl_state_init(&direct);
    ctl_set_allowed(&direct, HEATING_AND_NONE, 0.0, &config.core);
    ctl_observe(&direct, CTL_SIGNAL_T_OUTDOOR, 5.0, 0.0, &config.core);
    ctl_observe(&direct, CTL_SIGNAL_P_PRODUCED, 0.0, 0.0, &config.core);

    for (step = 1; step <= 60; step++) {
        const double now = 10.0 * step;
        const bool decided = ctl_loop_tick(&loop, &config, now, &record);
        double dt_s;
        (void)ctl_advance(&direct, now, &config.core, &dt_s);
        CHECK_TRUE(decided == (step % 6 == 0));
        if (decided) {
            ctl_decision expected;
            ctl_observe(&direct, CTL_SIGNAL_T_INDOOR, 22.5, now, &config.core);
            ctl_evaluate(&direct, now, &config.core, &expected);
            CHECK_DOUBLE(record.t, now);
            CHECK_STATUS(record.decision.demand, expected.demand);
            CHECK_STATUS(record.decision.reason, expected.reason);
            CHECK_STATUS(record.decision.phase, expected.phase);
            CHECK_DOUBLE(record.decision.energy_wh, expected.energy_wh);
            CHECK_DOUBLE(record.decision.dt_s, expected.dt_s);
            CHECK_TRUE(record.rooms.has_average);
            CHECK_DOUBLE(record.rooms.average_c, 22.5);
            decisions++;
        }
    }
    CHECK_UINT((uint32_t)decisions, 10u);
    /* The comparison means something only if the core left its cold start. */
    CHECK_STATUS(record.decision.phase, CTL_PHASE_RUNNING);
}

static void test_one_room_is_no_average(void)
{
    ctl_loop_config config;
    ctl_loop loop;
    ctl_loop_record record;
    int step;
    int decisions = 0;
    start(&loop, &config);
    CHECK_STATUS(give(&loop, &config, CTL_INPUT_ROOM, 2, "21.0", 0.0), CTL_INPUT_TAKEN);
    CHECK_STATUS(give(&loop, &config, CTL_INPUT_OUTDOOR, NO_ROOM, "5", 0.0), CTL_INPUT_TAKEN);
    CHECK_STATUS(give(&loop, &config, CTL_INPUT_HEAT, NO_ROOM, "0", 0.0), CTL_INPUT_TAKEN);
    for (step = 1; step <= 60; step++) {
        if (ctl_loop_tick(&loop, &config, 10.0 * step, &record)) {
            CHECK_TRUE(!record.rooms.has_average);
            CHECK_UINT((uint32_t)record.rooms.fresh_count, 1u);
            CHECK_STATUS(record.decision.reason, CTL_REASON_WAITING_FOR_INPUTS);
            CHECK_STATUS(record.decision.demand, CTL_DEMAND_NONE);
            decisions++;
        }
    }
    CHECK_UINT((uint32_t)decisions, 10u);
    CHECK_TRUE(!loop.state.signals.observed[CTL_SIGNAL_T_INDOOR]);
}

/* ---- The mask ---- */

typedef struct {
    int cooling;
    int masked;
} demand_count;

/* A warm house with the pump pouring heat in: the energy climbs past the hysteresis. */
static demand_count run_overheated_house(uint8_t allowed)
{
    static const house overheated = {22.0, 20.0, 30000.0};
    ctl_loop_config config;
    ctl_loop loop;
    ctl_loop_record record;
    demand_count count = {0, 0};
    int step;
    ctl_loop_config_defaults(&config, ROOMS);
    config.allowed = allowed;
    ctl_loop_init(&loop, &config, 0.0);
    for (step = 0; step < 360; step++) {
        const double now = 10.0 * step;
        if (step % 6 == 0) {
            report_house(&loop, &config, &overheated, now);
        }
        if (!ctl_loop_tick(&loop, &config, now, &record)) {
            continue;
        }
        if (record.decision.demand == CTL_DEMAND_COOLING) {
            count.cooling++;
        }
        if (record.decision.reason == CTL_REASON_MASKED) {
            CHECK_STATUS(record.decision.demand, CTL_DEMAND_NONE);
            count.masked++;
        }
    }
    return count;
}

static void test_cooling_is_masked(void)
{
    const demand_count masked = run_overheated_house(HEATING_AND_NONE);
    const demand_count unmasked = run_overheated_house(CTL_DEMAND_ALL);
    CHECK_TRUE(unmasked.cooling > 0);
    CHECK_UINT((uint32_t)masked.cooling, 0u);
    CHECK_TRUE(masked.masked > 0);
}

/* ---- The router ---- */

typedef struct {
    bool assume_landed;
    bool confirm_after_fault;
    int heat_sends;                /* thermostat commands of 2, the first and every repeat */
    bool faulted;
    ctl_router_status final_status;
} router_row;

static bool sends_heat(const ctl_commands *commands)
{
    return commands->count == 1 && strcmp(commands->command[0].name, THERMOSTAT) == 0 &&
           strcmp(commands->command[0].value, "2") == 0;
}

/* A cold house that calls for heat within the hour; the pump's report is the row's. */
static void run_router_row(const router_row *row)
{
    static const house cold = {17.0, -15.0, 0.0};
    ctl_loop_config config;
    ctl_loop loop;
    ctl_loop_record record;
    int heat_sends = 0;
    bool faulted = false;
    bool idle_after_send = false;
    bool heat_sent_last = false;
    int step;

    ctl_loop_config_defaults(&config, ROOMS);
    config.assume_landed = row->assume_landed;
    ctl_loop_init(&loop, &config, 0.0);
    for (step = 0; step <= 720; step++) {
        const double now = 10.0 * step;
        if (step % 6 == 0) {
            report_house(&loop, &config, &cold, now);
        }
        if (!ctl_loop_tick(&loop, &config, now, &record)) {
            continue;
        }
        if (heat_sent_last && record.router_status == CTL_ROUTER_IDLE) {
            idle_after_send = true;
        }
        heat_sent_last = sends_heat(&record.commands);
        if (heat_sent_last) {
            heat_sends++;
        }
        if (record.router_status == CTL_ROUTER_FAULT && record.decision.demand ==
                                                           CTL_DEMAND_HEATING) {
            faulted = true;
            if (row->confirm_after_fault) {
                ctl_loop_confirm(&loop, true, 2);
            }
        }
    }
    CHECK_UINT((uint32_t)heat_sends, (uint32_t)row->heat_sends);
    CHECK_TRUE(faulted == row->faulted);
    CHECK_STATUS(record.router_status, row->final_status);
    CHECK_TRUE(idle_after_send == row->assume_landed);
    CHECK_STATUS(record.decision.demand, CTL_DEMAND_HEATING);
}

static void test_router(void)
{
    static const router_row rows[] = {
        {true, false, 1, false, CTL_ROUTER_IDLE},
        {false, false, 4, true, CTL_ROUTER_FAULT},
        {false, true, 4, true, CTL_ROUTER_IDLE},
    };
    size_t i;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        run_router_row(&rows[i]);
    }
}

/* ---- The record's text ---- */

static void two_commands(ctl_commands *commands)
{
    commands->count = 2;
    strcpy(commands->command[0].name, THERMOSTAT);
    strcpy(commands->command[0].value, "2");
    strcpy(commands->command[1].name, HEAT_COOL);
    strcpy(commands->command[1].value, "0");
}

static ctl_loop_record record_with_inputs(void)
{
    ctl_loop_record record;
    memset(&record, 0, sizeof record);
    record.t = 600.0;
    record.decision.demand = CTL_DEMAND_HEATING;
    record.decision.reason = CTL_REASON_NORMAL;
    record.decision.phase = CTL_PHASE_RUNNING;
    record.decision.energy_wh = -1523.44;
    record.decision.has_inputs = true;
    record.decision.balance.p_net_w = -2735.4;
    record.decision.inputs.t_indoor_c = 22.456;
    record.decision.inputs.t_outdoor_c = 5.0;
    record.decision.inputs.p_produced_w = 3200.0;
    record.decision.inputs.p_solar_w = 20.0;
    record.rooms.has_average = true;
    record.rooms.average_c = 22.456;
    record.rooms.fresh_count = 3;
    record.rooms.fresh_weight = 3.0;
    record.rooms.total_weight = 4.0;
    two_commands(&record.commands);
    record.router_status = CTL_ROUTER_WAITING;
    return record;
}

static ctl_loop_record record_without_inputs(void)
{
    ctl_loop_record record;
    memset(&record, 0, sizeof record);
    record.t = 60.0;
    record.decision.demand = CTL_DEMAND_NONE;
    record.decision.reason = CTL_REASON_WAITING_FOR_INPUTS;
    record.decision.phase = CTL_PHASE_UNINITIALIZED;
    record.decision.has_inputs = false;
    record.rooms.fresh_count = 1;
    record.rooms.fresh_weight = 1.0;
    record.rooms.total_weight = 4.0;
    record.commands.count = 0;
    record.router_status = CTL_ROUTER_IDLE;
    return record;
}

/* The longest record the formatter writes: every number just below the fixed format's limit and
 * rounding up to thirteen digits, negative where its type allows, the longest name of every enum
 * and status, and both commands filling their fields without a terminator. */
static ctl_loop_record longest_record(void)
{
    ctl_loop_record record = record_with_inputs();
    size_t i;
    record.t = -999999999999.96;
    record.decision.demand = CTL_DEMAND_NONE;
    record.decision.reason = CTL_REASON_WAITING_FOR_INPUTS;
    record.decision.phase = CTL_PHASE_UNINITIALIZED;
    record.decision.energy_wh = -999999999999.96;
    record.decision.balance.p_net_w = -999999999999.6;
    record.decision.inputs.t_indoor_c = -999999999999.996;
    record.decision.inputs.t_outdoor_c = -999999999999.996;
    record.decision.inputs.p_produced_w = -999999999999.6;
    record.decision.inputs.p_solar_w = -999999999999.6;
    record.rooms.fresh_count = 999999999999u;
    record.rooms.fresh_weight = -999999999999.96;
    record.rooms.total_weight = -999999999999.96;
    record.router_status = CTL_ROUTER_WAITING;
    record.commands.count = CTL_ROUTER_COMMANDS_MAX;
    for (i = 0; i < CTL_ROUTER_COMMANDS_MAX; i++) {
        memset(record.commands.command[i].name, 'N', CTL_ROUTER_NAME_MAX);
        memset(record.commands.command[i].value, '9', CTL_ROUTER_VALUE_MAX);
    }
    return record;
}

#define LONGEST_NUMBER_1 "-1000000000000.0"
#define LONGEST_NUMBER_2 "-1000000000000.00"
#define LONGEST_NUMBER_0 "-1000000000000"
#define LONGEST_COMMAND "NNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNN:9999"
#define LONGEST_TEXT                                                                              \
    "t=" LONGEST_NUMBER_1 " demand=NO_DEMAND reason=WAITING_FOR_INPUTS phase=UNINITIALIZED"       \
    " E=" LONGEST_NUMBER_1 " pnet=" LONGEST_NUMBER_0 " indoor=" LONGEST_NUMBER_2                  \
    " outdoor=" LONGEST_NUMBER_2 " produced=" LONGEST_NUMBER_0 " solar=" LONGEST_NUMBER_0         \
    " rooms=999999999999:" LONGEST_NUMBER_1 "/" LONGEST_NUMBER_1 " router=WAITING"                \
    " cmd=" LONGEST_COMMAND "," LONGEST_COMMAND

typedef struct {
    ctl_loop_record record;
    const char *expected;
} text_row;

static void check_text_row(const char *file, int line, const text_row *row)
{
    char text[CTL_LOOP_TEXT_MAX];
    const size_t length = strlen(row->expected);
    check_true(file, line, ctl_loop_format_record(&row->record, text, sizeof text), "fits");
    check_text(file, line, text, row->expected);
    check_true(file, line, ctl_loop_format_record(&row->record, text, length + 1), "exact");
    check_text(file, line, text, row->expected);
    check_true(file, line, !ctl_loop_format_record(&row->record, text, length), "one short");
    check_text(file, line, text, "");
}

static void test_record_text(void)
{
    text_row rows[3];
    size_t i;
    rows[0].record = record_with_inputs();
    rows[0].expected = "t=600.0 demand=HEATING reason=NORMAL phase=RUNNING E=-1523.4 pnet=-2735 "
                       "indoor=22.46 outdoor=5.00 produced=3200 solar=20 rooms=3:3.0/4.0 "
                       "router=WAITING cmd=" THERMOSTAT ":2," HEAT_COOL ":0";
    rows[1].record = record_without_inputs();
    rows[1].expected = "t=60.0 demand=NO_DEMAND reason=WAITING_FOR_INPUTS phase=UNINITIALIZED "
                       "E=0.0 pnet=- indoor=- outdoor=- produced=- solar=- rooms=1:1.0/4.0 "
                       "router=IDLE cmd=-";
    rows[2].record = longest_record();
    rows[2].expected = LONGEST_TEXT;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        check_text_row(__FILE__, __LINE__, &rows[i]);
    }
}

/* A number the fixed format refuses cannot be written, whatever the room; the text is then
 * empty rather than cut. */
static void test_record_with_unwritable_number(void)
{
    ctl_loop_record record = record_with_inputs();
    char text[CTL_LOOP_TEXT_MAX];
    record.t = 1e12;
    strcpy(text, "stale");
    CHECK_TRUE(!ctl_loop_format_record(&record, text, sizeof text));
    CHECK_TEXT(text, "");
}

/* The constant is exactly the longest record's text and its terminating zero, so it holds every
 * record without wasting a byte. */
static void test_text_max_is_the_longest_record(void)
{
    CHECK_UINT((uint32_t)CTL_LOOP_TEXT_MAX, (uint32_t)(sizeof LONGEST_TEXT));
}

static void test_record_of_a_tick_fits(void)
{
    ctl_loop_config config;
    ctl_loop loop;
    ctl_loop_record record;
    char text[CTL_LOOP_TEXT_MAX];
    start(&loop, &config);
    CHECK_TRUE(ctl_loop_tick(&loop, &config, 60.0, &record));
    CHECK_TRUE(ctl_loop_format_record(&record, text, sizeof text));
    CHECK_TEXT(text, "t=60.0 demand=NO_DEMAND reason=WAITING_FOR_INPUTS phase=UNINITIALIZED "
                     "E=0.0 pnet=- indoor=- outdoor=- produced=- solar=- rooms=0:0.0/4.0 "
                     "router=WAITING cmd=" THERMOSTAT ":0");
}

int main(void)
{
    test_configuration();
    test_configuration_defaults();
    test_inputs();
    test_ticks();
    test_tick_refuses_not_finite();
    test_decisions_equal_the_core();
    test_one_room_is_no_average();
    test_cooling_is_masked();
    test_router();
    test_record_text();
    test_record_with_unwritable_number();
    test_text_max_is_the_longest_record();
    test_record_of_a_tick_fits();
    return check_result();
}
