// The shadow sketch: it runs the whole control path on a spare D1 mini, fed line by line from the
// house by shadow.py, and reports every decision as a line. The board is wired to nothing but the
// USB cable, so the commands in its records reach no pump. The sketch only moves lines and reads
// the clock; everything a line means is in ctl_shadow.c and every frame in ctl_frame.c, which the
// host twin runs too.

// arduino-cli links a library only when the sketch includes one of its headers, so each folder
// the loop calls into is named here, even where the sketch calls nothing of it.
extern "C" {
#include <ctl_clock.h>
#include <ctl_controller.h>
#include <ctl_payload.h>
#include <ctl_router.h>
#include <ctl_frame.h>
#include <ctl_loop.h>
#include <ctl_shadow.h>
}

// The speed the owner's D1 mini carries without dropping characters, as for the replay
// (docs/control/decisions.md, "The device replay runs at 230,400 baud").
static const unsigned long BAUD = 230400;
// A burst of retained messages at the start arrives as fast as the line carries it.
static const size_t RECEIVE_BUFFER = 2048;

static ctl_shadow shadow;
static ctl_clock board_clock;
// A full framed line and a carriage return; a longer line is answered with "error".
static char line[CTL_SHADOW_LINE_MAX + CTL_FRAME_SUFFIX + 2];
static size_t line_length;
static bool line_too_long;
static char reply[96];
// The longest record, its prefix, the heap field and the frame.
static char record[CTL_LOOP_TEXT_MAX + 2 + 16 + CTL_FRAME_SUFFIX];

// The framed "error", sent as it is when a line cannot be framed.
static const char FRAMED_ERROR[] = "error *78";

static void send_line(char *text, size_t size)
{
    if (ctl_frame_add(text, size)) {
        Serial.print(text);
    } else {
        Serial.print(FRAMED_ERROR);
    }
    Serial.print('\n');
}

static void send_text(const char *text)
{
    snprintf(reply, sizeof reply - CTL_FRAME_SUFFIX, "%s", text);
    send_line(reply, sizeof reply);
}

static uint64_t now_ms()
{
    return ctl_clock_widen(&board_clock, millis());
}

static double now_s()
{
    return (double)now_ms() / 1000.0;
}

static void end_line()
{
    size_t text_size = sizeof reply - CTL_FRAME_SUFFIX;
    line[line_length] = '\0';
    // A line whose frame does not check goes no further, so that a broken line never reaches
    // the loop.
    if (line_too_long || !ctl_frame_strip(line)) {
        snprintf(reply, text_size, "%s", "error");
    } else {
        ctl_shadow_handle(&shadow, line, now_s(), reply, text_size);
        if (strncmp(reply, "stats ", 6) == 0) {
            size_t length = strlen(reply);
            snprintf(reply + length, text_size - length, " heap=%lu uptime_s=%lu",
                     (unsigned long)ESP.getFreeHeap(), (unsigned long)(now_ms() / 1000));
        }
    }
    send_line(reply, sizeof reply);
    line_length = 0;
    line_too_long = false;
}

static void send_records()
{
    size_t text_size = sizeof record - CTL_FRAME_SUFFIX;
    while (ctl_shadow_poll(&shadow, now_s(), record, text_size)) {
        size_t length = strlen(record);
        snprintf(record + length, text_size - length, " heap=%lu",
                 (unsigned long)ESP.getFreeHeap());
        send_line(record, sizeof record);
    }
}

void setup()
{
    Serial.setRxBufferSize(RECEIVE_BUFFER);
    Serial.begin(BAUD);
    ctl_clock_init(&board_clock);
    ctl_shadow_init(&shadow);
    // The newline ends whatever the boot left on the line.
    Serial.print('\n');
    send_text("ready");
}

void loop()
{
    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        if (c == '\n') {
            end_line();
        } else if (line_length < sizeof line - 1) {
            line[line_length++] = c;
        } else {
            line_too_long = true;
        }
    }
    send_records();
}
