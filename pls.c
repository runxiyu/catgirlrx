/* Copyright (C) 2018  Curtis McEnroe <june@causal.agency>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

// From <https://en.cppreference.com/w/c/io/fwprintf#Notes>:
//
// While narrow strings provide snprintf, which makes it possible to determine
// the required output buffer size, there is no equivalent for wide strings
// (until C11's snwprintf_s), and in order to determine the buffer size, the
// program may need to call swprintf, check the result value, and reallocate a
// larger buffer, trying again until successful.
//
// snwprintf_s, unlike swprintf_s, will truncate the result to fit within the
// array pointed to by buffer, even though truncation is treated as an error by
// most bounds-checked functions.
int vaswprintf(wchar_t **ret, const wchar_t *format, va_list ap) {
	*ret = NULL;

	for (size_t cap = 2 * wcslen(format);; cap *= 2) {
		wchar_t *buf = realloc(*ret, sizeof(*buf) * (1 + cap));
		if (!buf) goto fail;
		*ret = buf;

		va_list _ap;
		va_copy(_ap, ap);
		int len = vswprintf(*ret, 1 + cap, format, _ap);
		va_end(_ap);

		if (len >= 0) return len;
		if (errno != EOVERFLOW) goto fail;
	}

fail:
	free(*ret);
	*ret = NULL;
	return -1;
}
