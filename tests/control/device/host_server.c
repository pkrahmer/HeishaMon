/*
 * The host twin of the device test sketch: it serves the line protocol over standard input and
 * output as the sketch serves it over the serial port, so that the replay script is proven on
 * the host before it talks to a board. It measures no time and has no heap to report, so those
 * fields of the stats line are 0.
 */
#include "wire/ctl_wire.h"

#include <stdio.h>
#include <string.h>

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

static bool is_stats_request(const char *line)
{
    return strcmp(line, "S") == 0 || strcmp(line, "S\r") == 0;
}

int main(void)
{
    static ctl_wire_session session;
    char line[CTL_WIRE_LINE_MAX + 2];
    char reply[CTL_WIRE_REPLY_SIZE];
    read_status status;
    ctl_wire_init(&session);
    puts("ready");
    fflush(stdout);
    while ((status = read_line(line, sizeof line)) != READ_END) {
        if (status == READ_TOO_LONG) {
            puts("error");
        } else if (is_stats_request(line)) {
            printf("stats calls=%lu us_total=0 us_max=0 heap=0 state_bytes=%lu\n",
                   (unsigned long)session.calls, (unsigned long)sizeof session);
        } else {
            ctl_wire_handle(&session, line, reply, sizeof reply);
            puts(reply);
        }
        fflush(stdout);
    }
    return 0;
}
