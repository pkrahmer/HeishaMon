/*
 * The host twin of the shadow sketch: it serves the shadow protocol over standard input and
 * output as the sketch serves it over the serial port, so that the bridge is proven on the host
 * before it talks to a board. Its clock is the host's monotonic clock since its start, multiplied
 * by --speed, so that a test covers many decision intervals in a few seconds of wall time. It has
 * no heap to report, so that field is 0.
 */
#define _POSIX_C_SOURCE 200809L

#include "loop/ctl_loop.h"
#include "loop/ctl_shadow.h"
#include "wire/ctl_frame.h"

#include <errno.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* The protocol asks for a poll at least every 10 ms of wall time. */
#define POLL_MS 10
/* A framed line of CTL_SHADOW_LINE_MAX characters and a carriage return, which the frame's check
 * ignores. */
#define LINE_SIZE (CTL_SHADOW_LINE_MAX + CTL_FRAME_SUFFIX + 2)
/* The longest record, its prefix, the heap field and the frame. */
#define RECORD_SIZE (CTL_LOOP_TEXT_MAX + 2 + 16 + CTL_FRAME_SUFFIX)
#define REPLY_SIZE 96
#define READ_CHUNK 256

/* The framed "error", sent as it is when a reply cannot be framed. */
#define FRAMED_ERROR "error *78"

typedef struct {
    char text[LINE_SIZE];
    size_t length;
    bool too_long;
} line_buffer;

static bool parse_arguments(int argc, char **argv, double *speed)
{
    char *end = NULL;
    *speed = 1.0;
    if (argc == 1) {
        return true;
    }
    if (argc != 3 || strcmp(argv[1], "--speed") != 0 || argv[2][0] == '\0') {
        return false;
    }
    errno = 0;
    *speed = strtod(argv[2], &end);
    return errno == 0 && *end == '\0' && isfinite(*speed) && *speed > 0.0;
}

static double wall_seconds(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}

/* Frames the text in line, whose buffer holds the text and the frame, and sends it. */
static void send_line(char *line, size_t size)
{
    if (ctl_frame_add(line, size)) {
        puts(line);
    } else {
        puts(FRAMED_ERROR);
    }
    fflush(stdout);
}

static void send_text(const char *text)
{
    char line[REPLY_SIZE];
    snprintf(line, sizeof line - CTL_FRAME_SUFFIX, "%s", text);
    send_line(line, sizeof line);
}

static void answer(ctl_shadow *shadow, line_buffer *buffer, double now)
{
    char reply[REPLY_SIZE];
    size_t text_size = sizeof reply - CTL_FRAME_SUFFIX;
    buffer->text[buffer->length] = '\0';
    if (buffer->too_long || !ctl_frame_strip(buffer->text)) {
        snprintf(reply, text_size, "%s", "error");
    } else {
        ctl_shadow_handle(shadow, buffer->text, now, reply, text_size);
        if (strncmp(reply, "stats ", strlen("stats ")) == 0) {
            size_t length = strlen(reply);
            snprintf(reply + length, text_size - length, " heap=0 uptime_s=%lu",
                     (unsigned long)now);
        }
    }
    send_line(reply, sizeof reply);
    buffer->length = 0;
    buffer->too_long = false;
}

static void take_bytes(ctl_shadow *shadow, line_buffer *buffer, const char *bytes, size_t count,
                       double now)
{
    size_t i;
    for (i = 0; i < count; i++) {
        if (bytes[i] == '\n') {
            answer(shadow, buffer, now);
        } else if (buffer->length < sizeof buffer->text - 1) {
            buffer->text[buffer->length++] = bytes[i];
        } else {
            buffer->too_long = true;
        }
    }
}

static void send_records(ctl_shadow *shadow, double now)
{
    char record[RECORD_SIZE];
    size_t text_size = sizeof record - CTL_FRAME_SUFFIX;
    while (ctl_shadow_poll(shadow, now, record, text_size)) {
        size_t length = strlen(record);
        snprintf(record + length, text_size - length, " heap=0");
        send_line(record, sizeof record);
    }
}

int main(int argc, char **argv)
{
    static ctl_shadow shadow;
    static line_buffer buffer;
    char bytes[READ_CHUNK];
    double speed;
    double started;
    if (!parse_arguments(argc, argv, &speed)) {
        fputs("usage: shadow_host [--speed <factor>]\n", stderr);
        return 2;
    }
    started = wall_seconds();
    ctl_shadow_init(&shadow);
    send_text("ready");
    for (;;) {
        struct pollfd input = {STDIN_FILENO, POLLIN, 0};
        int ready = poll(&input, 1, POLL_MS);
        double now = (wall_seconds() - started) * speed;
        if (ready > 0) {
            ssize_t count = read(STDIN_FILENO, bytes, sizeof bytes);
            if (count <= 0) {
                return 0;
            }
            take_bytes(&shadow, &buffer, bytes, (size_t)count, now);
        } else if (ready < 0 && errno != EINTR) {
            return 0;
        }
        send_records(&shadow, now);
    }
}
