#include "check.h"

#include "loop/ctl_format.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define BUFFER_SIZE 32
#define UNTOUCHED "untouched"

/* One call: the value, the decimals, the size offered (0: the whole buffer) and the text
 * expected, or NULL when the call must refuse and leave the buffer as it was. */
typedef struct {
    double value;
    int decimals;
    size_t size;
    const char *expected;
} format_row;

static void check_row(const char *file, int line, const format_row *row)
{
    char text[BUFFER_SIZE];
    const size_t size = row->size == 0 ? sizeof text : row->size;
    bool written;
    strcpy(text, UNTOUCHED);
    written = ctl_format_fixed(row->value, row->decimals, text, size);
    check_true(file, line, written == (row->expected != NULL), "written");
    check_text(file, line, text, row->expected != NULL ? row->expected : UNTOUCHED);
}

static void test_rows(void)
{
    const format_row rows[] = {
        {22.456, 2, 0, "22.46"},
        {-2735.0, 0, 0, "-2735"},
        {0.125, 2, 0, "0.13"},
        {-0.125, 2, 0, "-0.13"},
        {-0.004, 2, 0, "0.00"},
        {1500.0, 1, 0, "1500.0"},
        {0.0, 3, 0, "0.000"},
        {-0.0, 0, 0, "0"},
        {0.5, 0, 0, "1"},
        {-0.5, 0, 0, "-1"},
        {0.49999999999999994, 0, 0, "0"},
        {9.999, 2, 0, "10.00"},
        {-1.05, 3, 0, "-1.050"},
        {999999999999.9, 0, 0, "1000000000000"},
        {NAN, 2, 0, "nan"},
        {INFINITY, 1, 0, "inf"},
        {-INFINITY, 0, 0, "-inf"},
        {1e12, 0, 0, NULL},
        {-1e12, 2, 0, NULL},
        {1.0, 4, 0, NULL},
        {1.0, -1, 0, NULL},
        {NAN, 4, 0, NULL},
        {22.456, 2, 5, NULL},
        {22.456, 2, 6, "22.46"},
        {-INFINITY, 0, 4, NULL},
        {-INFINITY, 0, 5, "-inf"},
        {1.0, 0, 1, NULL},
    };
    size_t i;
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        check_row(__FILE__, __LINE__, &rows[i]);
    }
}

int main(void)
{
    test_rows();
    return check_result();
}
