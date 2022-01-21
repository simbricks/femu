/* Stolen from various places in QEMU source.
 * This source code is licensed under the GNU General Public License,
 * Version 2. */
#define _GNU_SOURCE

#include "../qemu-compat.h"

#include <stdio.h>
#include <stdarg.h>

void strpadcpy(char *buf, int buf_size, const char *str, char pad)
{
    int len = strnlen(str, buf_size);
    memcpy(buf, str, len);
    memset(buf + len, pad, buf_size - len);
}

size_t g_strlcpy(char *dest, const char *src, size_t size)
{
	size_t ret = strlen(src);

	if (size) {
		size_t len = (ret >= size) ? size - 1 : ret;
		memcpy(dest, src, len);
		dest[len] = '\0';
	}
	return ret;
}

char* g_strdup_printf(const char *format, ...)
{
    char *s = NULL;
    va_list val;

    va_start(val, format);
    vasprintf(&s, format, val);
    va_end(val);

    return s;
}