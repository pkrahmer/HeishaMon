/*
 * Constant text in flash. On the ESP8266 a constant lives in RAM unless it is placed in flash, and
 * the web server needs that RAM to build upstream's main page; text in flash must be read with
 * the flash-safe functions, a byte at a time or copied into RAM. On the host and on the ESP32 a
 * constant already stays out of RAM, so the same names are plain constants and the ordinary
 * functions, and every .c file still compiles as strict C99 on the host.
 *
 * The rule: a pointer to flash text is never read directly; it is passed only to the functions
 * below or to a function that says it takes flash text, and it is copied into the caller's buffer
 * before anything else reads it.
 */
#ifndef CTL_FLASH_H
#define CTL_FLASH_H

#if defined(ESP8266)

#include <pgmspace.h>

/* Places a constant object in flash. */
#define CTL_FLASH PROGMEM
/* A string literal placed in flash, as a const char pointer; only inside a function. */
#define CTL_FLASH_TEXT(literal) PSTR(literal)
/* The char at a flash address. */
#define CTL_FLASH_CHAR(address) ((char)pgm_read_byte(address))
/* strlen, strcmp (the first in RAM, the second in flash) and memcpy (from flash). */
#define CTL_FLASH_LENGTH(text) strlen_P(text)
#define CTL_FLASH_COMPARE(ram, flash) strcmp_P((ram), (flash))
#define CTL_FLASH_COPY(out, flash, size) memcpy_P((out), (flash), (size))

#else

#include <string.h>

#define CTL_FLASH
#define CTL_FLASH_TEXT(literal) (literal)
#define CTL_FLASH_CHAR(address) (*(const char *)(address))
#define CTL_FLASH_LENGTH(text) strlen(text)
#define CTL_FLASH_COMPARE(ram, flash) strcmp((ram), (flash))
#define CTL_FLASH_COPY(out, flash, size) memcpy((out), (flash), (size))

#endif

#endif
