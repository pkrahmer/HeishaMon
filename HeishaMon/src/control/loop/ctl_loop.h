/*
 * The loop: the one module the firmware glue calls. It takes each input as it arrives, runs it
 * through its adapter and hands any reading to the core at once, so that the core holds every
 * value from the instant it was measured. Called often with the current
 * instant, it advances the core every integration interval and, every tick interval, turns the
 * room average into one indoor reading, lets the core decide and steps the router. It is pure
 * like the core: instants are seconds of the caller's monotonic clock, and the commands it
 * returns are sent by the caller.
 */
#ifndef CTL_LOOP_H
#define CTL_LOOP_H

#include "../adapt/ctl_pump.h"
#include "../adapt/ctl_rooms.h"
#include "../core/ctl_config.h"
#include "../core/ctl_controller.h"
#include "../route/ctl_router.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Holds the text of every record ctl_loop_format_record can write, with its terminating zero: the
 * longest line has every number at the fixed format's widest, the longest name of every enum and
 * both commands filling their fields. */
#define CTL_LOOP_TEXT_MAX 365

typedef enum {
    CTL_INPUT_ROOM,
    CTL_INPUT_OUTDOOR,
    CTL_INPUT_HEAT,
    CTL_INPUT_VALVE,
    CTL_INPUT_DEFROST,
    CTL_INPUT_PV
} ctl_input;

typedef enum {
    CTL_INPUT_TAKEN,      /* accepted; a reading reached the core, or the room is stored */
    CTL_INPUT_NO_READING, /* accepted, but it changed nothing the core sees */
    CTL_INPUT_REFUSED,    /* the adapter refused the payload */
    CTL_INPUT_BAD         /* the call itself is wrong: input, room or instant */
} ctl_input_status;

typedef struct {
    ctl_config core;
    ctl_rooms_config rooms;
    ctl_router_config router;
    uint8_t allowed;    /* CTL_DEMAND_BIT of each allowed demand; default HEATING and NONE */
    bool assume_landed; /* default false; true: every command counts as landed at the next step */
} ctl_loop_config;

typedef struct {
    ctl_state state;
    ctl_rooms rooms;
    ctl_pump pump;
    ctl_router router;
    bool confirmed_known; /* the glue knows the thermostat state the pump reports */
    int confirmed_state;
    double next_advance;
    double next_evaluate;
} ctl_loop;

typedef struct {
    double t;
    ctl_decision decision;
    ctl_rooms_average rooms;
    ctl_commands commands;
    ctl_router_status router_status;
    int sent_state; /* the router's after the step; -1 before its first send */
} ctl_loop_record;

/* Writes the core's, the rooms' (for room_count rooms) and the router's defaults, allows heating
 * and no demand only, and does not assume landing. Refuses nothing; whether room_count is usable
 * is ctl_loop_config_valid's to say. */
void ctl_loop_config_defaults(ctl_loop_config *config, size_t room_count);

/* Returns true when ctl_config_validate finds nothing, the rooms' and the router's
 * configurations are valid and allowed has no bit beside the three demand bits. */
bool ctl_loop_config_valid(const ctl_loop_config *config);

/* Starts the loop cold at now: a fresh state, rooms, pump and router, the allowed demands set at
 * now, no confirmation, and both ticks one interval after now. Refuses nothing: a configuration
 * ctl_loop_config_valid refuses gives a loop that means nothing. */
void ctl_loop_init(ctl_loop *loop, const ctl_loop_config *config, double now);

/* Offers the length bytes of an input's payload, arrived at now, and returns what came of it.
 * A room is stored for the next decision tick (TAKEN) or refused by the rooms (REFUSED). Every
 * other input goes through its adapter: a reading reaches the core at once (TAKEN), an accepted
 * flag that changes no reading gives NO_READING, and a payload the adapter refuses gives
 * REFUSED. Refuses with BAD, changing nothing, an input that is no member, a room not below the
 * rooms' count and a now that is not finite. */
ctl_input_status ctl_loop_input(ctl_loop *loop, const ctl_loop_config *config, ctl_input input,
                                size_t room, const char *payload, size_t length, double now);

/* Stores whether the glue knows the thermostat state the pump reports, and that state; the
 * router reads it at the next decision tick unless the configuration assumes landing. Refuses
 * nothing. */
void ctl_loop_confirm(ctl_loop *loop, bool known, int state);

/* Advances the core when the integration tick is due and decides when the decision tick is
 * due. Returns true, with the decision, the room average, the commands to send, the router's
 * status and its sent state in *record, when it decided; false otherwise, leaving *record as it was. Refuses,
 * returning false and changing nothing, a now that is not finite. */
bool ctl_loop_tick(ctl_loop *loop, const ctl_loop_config *config, double now,
                   ctl_loop_record *record);

/* Writes the record as one line of text, its fields separated by single spaces, and returns
 * true. Refuses, returning false and leaving an empty text when size is not 0, a size that
 * cannot hold the text and its terminating zero, and a record holding a number that
 * ctl_format_fixed refuses (a magnitude of CTL_FORMAT_MAGNITUDE_LIMIT or more): a line is never
 * cut. */
bool ctl_loop_format_record(const ctl_loop_record *record, char *text, size_t size);

#endif
