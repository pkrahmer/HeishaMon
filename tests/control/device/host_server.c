/*
 * The host twin of the device test sketch: it serves the line protocol over standard input and
 * output as the sketch serves it over the serial port, so that the replay script is proven on
 * the host before it talks to a board. It measures no time and has no heap to report, so those
 * fields of the stats line are 0.
 *
 * With --garble <n> it breaks the frame of its n-th reply after "ready" on purpose, as a serial
 * line would, so that the script's restart is proven without a board.
 */
#include "wire/ctl_frame.h"
#include "wire/ctl_wire.h"

#include <stdio.h>
#include <string.h>

/* More digits than this are refused: no replay comes near a billion replies. */
#define GARBLE_DIGITS_MAX 9

typedef enum { READ_LINE, READ_TOO_LONG, READ_END } read_status;

/* Reads one line without its newline. A line that does not fit is read to its end and
 * reported, so that the next line starts where it should. */
static read_status read_line(char *line, size_t size)
{
    size_t length;
    int c;
    if (fgets(line, (int)size, stdin) == NULL) {
        return READ_END;
    }
    length = strlen(line);
    if (length > 0 && line[length - 1] == '\n') {
        line[length - 1] = '\0';
        return READ_LINE;
    }
    if (length < size - 1) {
        return READ_LINE;
    }
    do {
        c = getchar();
    } while (c != '\n' && c != EOF);
    return READ_TOO_LONG;
}

/* Returns true and sets *garble from the command line: no option, or --garble and a number of
 * at most GARBLE_DIGITS_MAX decimal digits, where 0 garbles nothing. */
static bool parse_arguments(int argc, char **argv, unsigned long *garble)
{
    size_t i;
    size_t length;
    *garble = 0;
    if (argc == 1) {
        return true;
    }
    if (argc != 3 || strcmp(argv[1], "--garble") != 0) {
        return false;
    }
    length = strlen(argv[2]);
    if (length == 0 || length > GARBLE_DIGITS_MAX) {
        return false;
    }
    for (i = 0; i < length; i++) {
        if (argv[2][i] < '0' || argv[2][i] > '9') {
            return false;
        }
        *garble = *garble * 10 + (unsigned long)(argv[2][i] - '0');
    }
    return true;
}

/* The framed "error", sent as it is when a reply cannot be framed. */
#define FRAMED_ERROR "error *78"

/* Frames the text in reply, whose buffer holds the text and the frame, and sends it. A garbled
 * line keeps the checksum of the text it had, so its frame no longer checks. A reply that cannot
 * be framed is sent as a framed "error": the script restarts the trace on it rather than reading
 * an unframed line. */
static void send_line(char *reply, size_t size, bool garbled)
{
    if (!ctl_frame_add(reply, size)) {
        puts(FRAMED_ERROR);
        fflush(stdout);
        return;
    }
    if (garbled) {
        reply[0] = '#';
    }
    puts(reply);
    fflush(stdout);
}

/* Writes the reply's text into reply, leaving CTL_FRAME_SUFFIX bytes of its size for the
 * frame. */
static void answer(ctl_wire_session *session, char *line, read_status status, char *reply,
                   size_t size)
{
    size_t text_size = size - CTL_FRAME_SUFFIX;
    if (status == READ_TOO_LONG || !ctl_frame_strip(line)) {
        snprintf(reply, text_size, "%s", "error");
    } else if (strcmp(line, "S") == 0) {
        snprintf(reply, text_size, "stats calls=%lu us_total=0 us_max=0 heap=0 state_bytes=%lu",
                 (unsigned long)session->calls, (unsigned long)sizeof *session);
    } else {
        ctl_wire_handle(session, line, reply, text_size);
    }
}

int main(int argc, char **argv)
{
    static ctl_wire_session session;
    /* A framed line of CTL_WIRE_LINE_MAX characters, its newline and the zero. */
    char line[CTL_WIRE_LINE_MAX + CTL_FRAME_SUFFIX + 2];
    char reply[CTL_WIRE_REPLY_SIZE + CTL_FRAME_SUFFIX];
    unsigned long garble;
    unsigned long replies = 0;
    read_status status;
    if (!parse_arguments(argc, argv, &garble)) {
        fputs("usage: host_server [--garble <n>]\n", stderr);
        return 2;
    }
    ctl_wire_init(&session);
    snprintf(reply, sizeof reply - CTL_FRAME_SUFFIX, "%s", "ready");
    send_line(reply, sizeof reply, false);
    while ((status = read_line(line, sizeof line)) != READ_END) {
        answer(&session, line, status, reply, sizeof reply);
        replies++;
        send_line(reply, sizeof reply, replies == garble);
    }
    return 0;
}
