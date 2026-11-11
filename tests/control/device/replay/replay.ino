// The device test sketch: it serves the control core over the USB serial port with the line
// protocol of ctl_wire.h, so that replay.py can feed it the golden traces and compare every bit
// the board computes with what the host computed. The sketch only moves lines and measures
// time; everything the protocol means is in ctl_wire.c, which the host twin runs too.

extern "C" {
#include <ctl_config.h>
#include <ctl_controller.h>
#include <ctl_wire.h>
}

static const unsigned long BAUD = 921600;
// Up to 16 requests are in flight, each well below 64 bytes.
static const size_t RECEIVE_BUFFER = 1024;

static ctl_wire_session session;
// A full line and a carriage return; a longer line is answered with "error".
static char line[CTL_WIRE_LINE_MAX + 2];
static size_t line_length;
static bool line_too_long;
static char reply[CTL_WIRE_REPLY_SIZE];
static uint32_t us_total;
static uint32_t us_max;

static bool is_call(const char *request)
{
    return request[0] == 'O' || request[0] == 'A' || request[0] == 'M' || request[0] == 'E';
}

static void send_stats()
{
    Serial.print("stats calls=");
    Serial.print((unsigned long)session.calls);
    Serial.print(" us_total=");
    Serial.print((unsigned long)us_total);
    Serial.print(" us_max=");
    Serial.print((unsigned long)us_max);
    Serial.print(" heap=");
    Serial.print((unsigned long)ESP.getFreeHeap());
    Serial.print(" state_bytes=");
    Serial.print((unsigned long)sizeof session);
    Serial.print('\n');
}

static void handle(const char *request)
{
    uint32_t started = micros();
    ctl_wire_handle(&session, request, reply, sizeof reply);
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
    Serial.print(reply);
    Serial.print('\n');
}

static void end_line()
{
    line[line_length] = '\0';
    if (line_too_long) {
        Serial.print("error\n");
    } else if (strcmp(line, "S") == 0 || strcmp(line, "S\r") == 0) {
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
    Serial.print("\nready\n");
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
