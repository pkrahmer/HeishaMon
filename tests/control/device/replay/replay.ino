// The device test sketch: it serves the control core over the USB serial port with the line
// protocol of ctl_wire.h, so that replay.py can feed it the golden traces and compare every bit
// the board computes with what the host computed. The sketch only moves lines and measures
// time; everything the protocol means is in ctl_wire.c and every frame in ctl_frame.c, which
// the host twin runs too.

extern "C" {
#include <ctl_config.h>
#include <ctl_controller.h>
#include <ctl_frame.h>
#include <ctl_wire.h>
}

// The speed the owner's D1 mini carries without dropping characters, with the script's 4
// requests in flight (docs/control/decisions.md, "The device replay runs at 230,400 baud").
static const unsigned long BAUD = 230400;
// Up to 4 requests are in flight, each well below 64 bytes; the buffer leaves ample room.
static const size_t RECEIVE_BUFFER = 1024;

static ctl_wire_session session;
// A full framed line and a carriage return; a longer line is answered with "error".
static char line[CTL_WIRE_LINE_MAX + CTL_FRAME_SUFFIX + 2];
static size_t line_length;
static bool line_too_long;
static char reply[CTL_WIRE_REPLY_SIZE + CTL_FRAME_SUFFIX];
static uint32_t us_total;
static uint32_t us_max;

static bool is_call(const char *request)
{
    return request[0] == 'O' || request[0] == 'A' || request[0] == 'M' || request[0] == 'E';
}

// The framed "error", sent as it is when a reply cannot be framed.
static const char FRAMED_ERROR[] = "error *78";

// A reply that cannot be framed is sent as a framed "error": the script restarts the trace on it
// rather than reading an unframed line.
static void send_reply()
{
    if (ctl_frame_add(reply, sizeof reply)) {
        Serial.print(reply);
    } else {
        Serial.print(FRAMED_ERROR);
    }
    Serial.print('\n');
}

static void send_text(const char *text)
{
    snprintf(reply, sizeof reply - CTL_FRAME_SUFFIX, "%s", text);
    send_reply();
}

static void send_stats()
{
    snprintf(reply, sizeof reply - CTL_FRAME_SUFFIX,
             "stats calls=%lu us_total=%lu us_max=%lu heap=%lu state_bytes=%lu",
             (unsigned long)session.calls, (unsigned long)us_total, (unsigned long)us_max,
             (unsigned long)ESP.getFreeHeap(), (unsigned long)sizeof session);
    send_reply();
}

static void handle(const char *request)
{
    uint32_t started = micros();
    ctl_wire_handle(&session, request, reply, sizeof reply - CTL_FRAME_SUFFIX);
    uint32_t spent = micros() - started;
    bool answered = strcmp(reply, "error") != 0;
    if (answered && request[0] == 'B' && strcmp(reply, "ok") == 0) {
        us_total = 0;
        us_max = 0;
    } else if (answered && is_call(request)) {
        us_total += spent;
        if (spent > us_max) {
            us_max = spent;
        }
    }
    send_reply();
}

static void end_line()
{
    line[line_length] = '\0';
    // A request whose frame does not check is answered and goes no further, so that a broken
    // line never reaches the core or the time sums.
    if (line_too_long || !ctl_frame_strip(line)) {
        send_text("error");
    } else if (strcmp(line, "S") == 0) {
        send_stats();
    } else {
        handle(line);
    }
    line_length = 0;
    line_too_long = false;
}

void setup()
{
    Serial.setRxBufferSize(RECEIVE_BUFFER);
    Serial.begin(BAUD);
    ctl_wire_init(&session);
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
}
