#include "check.h"

#include "ctl_flash.h"
#include "loop/ctl_text.h"

#include <stddef.h>

/* A text in flash is appended as a text in RAM is. */
static void test_flash_piece_appends(void)
{
    char buffer[16];
    ctl_text writer;
    ctl_text_start(&writer, buffer, sizeof buffer);
    ctl_text_put(&writer, "a=");
    ctl_text_put_flash(&writer, CTL_FLASH_TEXT("flash"));
    CHECK_TRUE(writer.fits);
    CHECK_TEXT(buffer, "a=flash");
    CHECK_UINT(writer.length, 7);
}

/* A flash piece that does not fit is refused as ctl_text_put refuses it: the text keeps what
 * fitted, and nothing is written after. */
static void test_flash_piece_refused(void)
{
    char buffer[6];
    ctl_text writer;
    ctl_text_start(&writer, buffer, sizeof buffer);
    ctl_text_put_flash(&writer, CTL_FLASH_TEXT("too long"));
    CHECK_TRUE(!writer.fits);
    CHECK_TEXT(buffer, "too l");
    ctl_text_put_flash(&writer, CTL_FLASH_TEXT(""));
    ctl_text_put(&writer, "x");
    CHECK_TEXT(buffer, "too l");
}

static void test_empty_buffer_is_never_written(void)
{
    char buffer[1] = {'?'};
    ctl_text writer;
    ctl_text_start(&writer, buffer, 0);
    ctl_text_put_flash(&writer, CTL_FLASH_TEXT("a"));
    CHECK_TRUE(!writer.fits);
    CHECK_UINT((unsigned char)buffer[0], '?');
}

int main(void)
{
    test_flash_piece_appends();
    test_flash_piece_refused();
    test_empty_buffer_is_never_written();
    return check_result();
}
