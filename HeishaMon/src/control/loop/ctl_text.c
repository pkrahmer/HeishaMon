#include "ctl_text.h"

#include "../route/ctl_router.h"
#include "ctl_format.h"

#include <stdbool.h>
#include <stddef.h>

#define NOTHING "-"

void ctl_text_start(ctl_text *writer, char *buffer, size_t size)
{
    writer->text = buffer;
    writer->size = size;
    writer->length = 0;
    writer->fits = size > 0;
    if (size > 0) {
        buffer[0] = '\0';
    }
}

void ctl_text_put_bounded(ctl_text *writer, const char *piece, size_t limit)
{
    size_t i;
    for (i = 0; writer->fits && i < limit && piece[i] != '\0'; i++) {
        if (writer->length + 1 >= writer->size) {
            writer->fits = false;
            return;
        }
        writer->text[writer->length++] = piece[i];
        writer->text[writer->length] = '\0';
    }
}

void ctl_text_put(ctl_text *writer, const char *piece)
{
    ctl_text_put_bounded(writer, piece, (size_t)-1);
}

void ctl_text_put_number(ctl_text *writer, double value, int decimals)
{
    char number[CTL_FORMAT_TEXT_MAX];
    if (!ctl_format_fixed(value, decimals, number, sizeof number)) {
        writer->fits = false;
        return;
    }
    ctl_text_put(writer, number);
}

void ctl_text_put_input(ctl_text *writer, bool has_inputs, double value, int decimals)
{
    if (has_inputs) {
        ctl_text_put_number(writer, value, decimals);
    } else {
        ctl_text_put(writer, NOTHING);
    }
}

void ctl_text_put_commands(ctl_text *writer, const ctl_commands *commands)
{
    size_t i;
    if (commands->count == 0) {
        ctl_text_put(writer, NOTHING);
        return;
    }
    for (i = 0; i < commands->count && i < CTL_ROUTER_COMMANDS_MAX; i++) {
        if (i > 0) {
            ctl_text_put(writer, ",");
        }
        ctl_text_put_bounded(writer, commands->command[i].name, CTL_ROUTER_NAME_MAX);
        ctl_text_put(writer, ":");
        ctl_text_put_bounded(writer, commands->command[i].value, CTL_ROUTER_VALUE_MAX);
    }
}
