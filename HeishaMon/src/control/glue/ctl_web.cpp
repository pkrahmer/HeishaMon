#include "ctl_web.h"

#include <Arduino.h>
// The board's network headers come before upstream's web server, whose lwip includes would
// otherwise break the definition of IPAddress.
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif

#include "../../../version.h"
#include "../../../htmlcode.h"
#include "../../common/webserver.h"
#include "ctl_firmware.h"

extern "C" {
#include "ctl_form.h"
#include "ctl_html.h"
#include "ctl_settings.h"
#include "ctl_telemetry.h"
}

#include <stdlib.h>
#include <string.h>

// Upstream's routes stop at 200; these two are the page's own.
static const uint8_t ROUTE_PAGE = 240;
static const uint8_t ROUTE_SAVE = 241;

// The largest piece of the page held at once; webserver_send_content copies it.
static const size_t CHUNK_SIZE = 512;

// --- The save ------------------------------------------------------------------------------

// What the last save did to each setting it held, for the page that follows it.
static ctl_form_report last_report;

static ctl_setting_status change_setting(void *context, const char *name, const char *value,
                                         bool *changed)
{
    (void)context;
    return ctl_firmware_change(name, value, changed);
}

static void apply_form(const ctl_form *form)
{
    const ctl_settings *settings = ctl_firmware_settings();
    if (settings == NULL) {
        ctl_form_report_start(&last_report);
        return;
    }
    // The form holds every topic, so any save may move one; a field posted with the value it
    // already had is no reason to write the file.
    ctl_firmware_change_begin(true, false);
    bool changed = ctl_form_apply(form, settings, change_setting, NULL, &last_report);
    ctl_firmware_change_end();
    if (changed) {
        ctl_firmware_publish_settings();
    }
}

// --- The page ------------------------------------------------------------------------------

static const char PAGE_TOP[] FLASHPROG = R"====(
<script>
document.addEventListener('DOMContentLoaded',function(){
  var nav=document.getElementById('sideNav');
  nav.innerHTML=`
<a href="/"><span class="nav-icon">&#8634;</span> Home</a>
<a href="/settings"><span class="nav-icon">&#9881;</span> Settings</a>
<a href="/rules"><span class="nav-icon">&#8881;</span> Rules</a>
`;
});
</script>
<main class='main-content'>
<h1 style='color:var(--accent);margin-bottom:16px'><span aria-hidden='true'>&#9832;</span>
Control loop</h1>
)====";

typedef enum {
    SECTION_STATUS,
    SECTION_INPUTS,
    SECTION_FORM,
    SECTION_END,
    SECTION_COUNT
} section;

typedef struct {
    const char *open;
    const char *close;
    const char *absent; // written instead when the section is not shown
} section_text;

static const char TABLE_CLOSE[] FLASHPROG = "</table></div>";
static const char STATUS_OPEN[] FLASHPROG = "<h2>Status</h2><div class='panel'><table>";
static const char NO_DECISION[] FLASHPROG = "<p>No decision has been made yet.</p>";
static const char INPUTS_OPEN[] FLASHPROG = "<h2>Inputs</h2><div class='panel'><table>";
static const char FORM_OPEN[] FLASHPROG = CTL_HTML_SETTINGS_OPEN;
static const char FORM_CLOSE[] FLASHPROG = CTL_HTML_SETTINGS_CLOSE;
static const char NOT_STARTED[] FLASHPROG = "<p>The control loop has not started.</p>";
static const char PAGE_END[] FLASHPROG = "</main>";
static const char EMPTY[] FLASHPROG = "";

static const section_text SECTION_TEXT[SECTION_COUNT] = {
    {STATUS_OPEN, TABLE_CLOSE, NO_DECISION},
    {INPUTS_OPEN, TABLE_CLOSE, EMPTY},
    {FORM_OPEN, FORM_CLOSE, NOT_STARTED},
    {EMPTY, EMPTY, PAGE_END},
};

// What one content step of the page reads: the glue's state at that step.
typedef struct {
    bool decided;
    ctl_telemetry_source source;
    const ctl_settings *settings;
} page_view;

typedef struct {
    bool shown;
    size_t rows;
} section_shape;

// Where a page stands between two content steps, kept in the client's userdata: the section
// above ITEM_BITS, and below them the item, 0 for the opening text, then one per row, then the
// closing text. No section has 255 rows.
static const unsigned int ITEM_BITS = 8;
static const uintptr_t ITEM_MASK = (1u << ITEM_BITS) - 1;
static const uintptr_t FINISHED = (uintptr_t)SECTION_COUNT << ITEM_BITS;

static section_shape shape_of(const page_view *view, section part)
{
    section_shape shape = {false, 0};
    switch (part) {
    case SECTION_STATUS:
        shape.shown = view->decided;
        if (shape.shown) {
            shape.rows = ctl_telemetry_count(&view->source, CTL_GROUP_STATUS)
                         + ctl_telemetry_count(&view->source, CTL_GROUP_OUTPUTS);
        }
        break;
    case SECTION_INPUTS:
        shape.shown = view->decided;
        if (shape.shown) {
            shape.rows = ctl_telemetry_count(&view->source, CTL_GROUP_INPUTS);
        }
        break;
    case SECTION_FORM:
        shape.shown = view->settings != NULL;
        if (shape.shown) {
            shape.rows = ctl_settings_name_count(view->settings);
        }
        break;
    case SECTION_END:
    case SECTION_COUNT:
        break;
    }
    return shape;
}

static bool put_flash(const char *text, char *out, size_t size)
{
    size_t length = strlen_P(text);
    if (length >= size) {
        return false;
    }
    memcpy_P(out, text, length + 1);
    return true;
}

// An entry the formatter refuses is left out; one too long for a chunk shows its name only.
static bool telemetry_row(const ctl_telemetry_source *source, ctl_group group, size_t index,
                          char *out, size_t size, bool alone)
{
    char subtopic[CTL_TELEMETRY_SUBTOPIC_MAX];
    char value[CTL_SETTINGS_VALUE_MAX];
    out[0] = '\0';
    if (!ctl_telemetry_entry(source, group, index, subtopic, sizeof subtopic,
                             value, sizeof value)) {
        return true;
    }
    return ctl_html_row(subtopic, value, out, size)
           || (alone && ctl_html_row(subtopic, CTL_HTML_TOO_LONG, out, size));
}

static bool status_row(const ctl_telemetry_source *source, size_t index, char *out, size_t size,
                       bool alone)
{
    size_t status_count = ctl_telemetry_count(source, CTL_GROUP_STATUS);
    if (index < status_count) {
        return telemetry_row(source, CTL_GROUP_STATUS, index, out, size, alone);
    }
    return telemetry_row(source, CTL_GROUP_OUTPUTS, index - status_count, out, size, alone);
}

// A value too long to show escaped in one chunk gets no input, so a save leaves it as it is.
static bool setting_row(const ctl_settings *settings, size_t index, char *out, size_t size,
                        bool alone)
{
    return ctl_form_row(settings, &last_report, index, true, out, size)
           || (alone && ctl_form_row(settings, &last_report, index, false, out, size));
}

static bool render_row(const page_view *view, section part, size_t index, char *out, size_t size,
                       bool alone)
{
    switch (part) {
    case SECTION_STATUS:
        return status_row(&view->source, index, out, size, alone);
    case SECTION_INPUTS:
        return telemetry_row(&view->source, CTL_GROUP_INPUTS, index, out, size, alone);
    case SECTION_FORM:
        return setting_row(view->settings, index, out, size, alone);
    case SECTION_END:
    case SECTION_COUNT:
        break;
    }
    out[0] = '\0';
    return true;
}

// Writes the item the state names into out; alone is true when out is a whole empty chunk.
static bool render_item(const page_view *view, uintptr_t state, char *out, size_t size,
                        bool alone)
{
    section part = (section)(state >> ITEM_BITS);
    size_t item = state & ITEM_MASK;
    section_shape shape = shape_of(view, part);
    const section_text *text = &SECTION_TEXT[part];
    if (!shape.shown) {
        return put_flash(text->absent, out, size);
    }
    if (item == 0) {
        return put_flash(text->open, out, size);
    }
    if (item <= shape.rows) {
        return render_row(view, part, item - 1, out, size, alone);
    }
    return put_flash(text->close, out, size);
}

// A section whose rows shrank between two steps still ends with its closing text, once.
static uintptr_t next_state(const page_view *view, uintptr_t state)
{
    section part = (section)(state >> ITEM_BITS);
    section_shape shape = shape_of(view, part);
    if (!shape.shown || (state & ITEM_MASK) >= shape.rows + 1) {
        return (uintptr_t)(part + 1) << ITEM_BITS;
    }
    return state + 1;
}

static void write_page_top(struct webserver_t *client)
{
    webserver_send(client, 200, (char *)"text/html", 0);
    webserver_send_content_P(client, webHeader, strlen_P(webHeader));
    webserver_send_content_P(client, webCSS, strlen_P(webCSS));
    webserver_send_content_P(client, webBodyStart, strlen_P(webBodyStart));
    webserver_send_content_P(client, PAGE_TOP, strlen_P(PAGE_TOP));
    client->userdata = (void *)(uintptr_t)0;
}

// Fills one chunk with as many whole items as fit. A step that sends nothing ends the page, so
// every step before the end sends at least one item.
static void write_page_part(struct webserver_t *client)
{
    page_view view;
    view.decided = ctl_firmware_last_decision(&view.source);
    view.settings = ctl_firmware_settings();
    uintptr_t state = (uintptr_t)client->userdata;
    char chunk[CHUNK_SIZE];
    size_t length = 0;
    // Each pass consumes one item, and the page has far fewer items than this.
    for (size_t pass = 0; pass < 4 * CHUNK_SIZE && state < FINISHED; pass++) {
        bool alone = length == 0;
        if (!render_item(&view, state, chunk + length, sizeof chunk - length, alone)) {
            if (!alone) {
                break;
            }
            chunk[0] = '\0';
        }
        length += strlen(chunk + length);
        state = next_state(&view, state);
    }
    if (length > 0) {
        webserver_send_content(client, chunk, (uint16_t)length);
    }
    if (state >= FINISHED && (uintptr_t)client->userdata < FINISHED) {
        webserver_send_content_P(client, menuJS, strlen_P(menuJS));
        webserver_send_content_P(client, webFooter, strlen_P(webFooter));
        // The results belong to the page that follows the save; a reload shows none.
        ctl_form_report_start(&last_report);
    }
    client->userdata = (void *)state;
}

// --- The steps -----------------------------------------------------------------------------

static int8_t claim_route(struct webserver_t *client, const char *uri, bool *claimed)
{
    *claimed = true;
    if (strcmp_P(uri, PSTR("/control")) == 0) {
        client->route = ROUTE_PAGE;
        return 0;
    }
    if (strcmp_P(uri, PSTR("/savecontrol")) == 0) {
        ctl_form *form = (ctl_form *)malloc(sizeof(ctl_form));
        if (form == NULL) {
            return -1;
        }
        ctl_form_start(form);
        client->route = ROUTE_SAVE;
        client->userdata = form;
        return 0;
    }
    *claimed = false;
    return 0;
}

static void release(struct webserver_t *client)
{
    if (client->route == ROUTE_SAVE && client->userdata != NULL) {
        free(client->userdata);
    }
    client->userdata = NULL;
}

static int8_t page_step(struct webserver_t *client)
{
    if (client->step == WEBSERVER_CLIENT_WRITE) {
        if (client->content == 0) {
            write_page_top(client);
        } else {
            write_page_part(client);
        }
    } else if (client->step == WEBSERVER_CLIENT_CLOSE) {
        release(client);
    }
    return 0;
}

static int8_t save_step(struct webserver_t *client, void *dat)
{
    ctl_form *form = (ctl_form *)client->userdata;
    switch (client->step) {
    case WEBSERVER_CLIENT_ARGS:
        // A connection lost while the body arrives closes with this step and no argument.
        if (dat == NULL) {
            release(client);
        } else if (form != NULL) {
            const arguments_t *args = (const arguments_t *)dat;
            ctl_form_keep(form, (const char *)args->name, (const char *)args->value, args->len);
        }
        return 0;
    case WEBSERVER_CLIENT_WRITE:
        // Upstream clears userdata before the close step of a finished request, so the buffer
        // is freed here; the other releases cover a connection lost before this step.
        if (client->content == 0) {
            if (form != NULL) {
                apply_form(form);
            }
            release(client);
            webserver_send(client, 301, (char *)"text/plain", 0);
        }
        return 0;
    case WEBSERVER_CLIENT_CREATE_HEADER: {
        struct header_t *header = (struct header_t *)dat;
        header->ptr += sprintf_P((char *)header->buffer, PSTR("Location: /control"));
        return -1;
    }
    case WEBSERVER_CLIENT_CLOSE:
        release(client);
        return 0;
    default:
        return 0;
    }
}

bool ctl_web(struct webserver_t *client, void *dat, int8_t *result)
{
    if (client->step == WEBSERVER_CLIENT_REQUEST_URI) {
        bool claimed;
        *result = claim_route(client, (const char *)dat, &claimed);
        return claimed;
    }
    if (client->route == ROUTE_PAGE && client->step != WEBSERVER_CLIENT_REQUEST_METHOD) {
        *result = page_step(client);
        return true;
    }
    if (client->route == ROUTE_SAVE && client->step != WEBSERVER_CLIENT_REQUEST_METHOD) {
        *result = save_step(client, dat);
        return true;
    }
    return false;
}
