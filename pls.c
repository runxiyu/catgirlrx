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

#include "chat.h"

wchar_t *wcsnchr(const wchar_t *wcs, size_t len, wchar_t chr) {
	len = wcsnlen(wcs, len);
	for (size_t i = 0; i < len; ++i) {
		if (wcs[i] == chr) return (wchar_t *)&wcs[i];
	}
	return NULL;
}

wchar_t *wcsnrchr(const wchar_t *wcs, size_t len, wchar_t chr) {
	len = wcsnlen(wcs, len);
	for (size_t i = len - 1; i < len; --i) {
		if (wcs[i] == chr) return (wchar_t *)&wcs[i];
	}
	return NULL;
}

wchar_t *ambstowcs(const char *src) {
	size_t len = mbsrtowcs(NULL, &src, 0, NULL);
	if (len == (size_t)-1) return NULL;

	wchar_t *dst = malloc(sizeof(*dst) * (1 + len));
	if (!dst) return NULL;

	len = mbsrtowcs(dst, &src, len, NULL);
	if (len == (size_t)-1) {
		free(dst);
		return NULL;
	}

	dst[len] = L'\0';
	return dst;
}

char *awcstombs(const wchar_t *src) {
	size_t len = wcsrtombs(NULL, &src, 0, NULL);
	if (len == (size_t)-1) return NULL;

	char *dst = malloc(sizeof(*dst) * (1 + len));
	if (!dst) return NULL;

	len = wcsrtombs(dst, &src, len, NULL);
	if (len == (size_t)-1) {
		free(dst);
		return NULL;
	}

	dst[len] = '\0';
	return dst;
}

char *awcsntombs(const wchar_t *src, size_t nwc) {
	size_t len = wcsnrtombs(NULL, &src, nwc, 0, NULL);
	if (len == (size_t)-1) return NULL;

	char *dst = malloc(sizeof(*dst) * (1 + len));
	if (!dst) return NULL;

	len = wcsnrtombs(dst, &src, nwc, len, NULL);
	if (len == (size_t)-1) {
		free(dst);
		return NULL;
	}

	dst[len] = '\0';
	return dst;
}

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
		errno = EOVERFLOW; // vswprintf may not set errno.
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

int aswprintf(wchar_t **ret, const wchar_t *format, ...) {
	va_list ap;
	va_start(ap, format);
	int n = vaswprintf(ret, format, ap);
	va_end(ap);
	return n;
}

static const char Base64[64] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char *base64(const byte *src, size_t len) {
	char *dst = malloc(1 + 4 * (len + 2) / 3);
	if (!dst) return NULL;
	size_t i = 0;
	while (len > 2) {
		dst[i++] = Base64[0x3F & (src[0] >> 2)];
		dst[i++] = Base64[0x3F & (src[0] << 4 | src[1] >> 4)];
		dst[i++] = Base64[0x3F & (src[1] << 2 | src[2] >> 6)];
		dst[i++] = Base64[0x3F & src[2]];
		src += 3;
		len -= 3;
	}
	if (len) {
		dst[i++] = Base64[0x3F & (src[0] >> 2)];
		if (len > 1) {
			dst[i++] = Base64[0x3F & (src[0] << 4 | src[1] >> 4)];
			dst[i++] = Base64[0x3F & (src[1] << 2)];
		} else {
			dst[i++] = Base64[0x3F & (src[0] << 4)];
			dst[i++] = '=';
		}
		dst[i++] = '=';
	}
	dst[i] = '\0';
	return dst;
}

#ifdef TEST
#include <assert.h>
#include <string.h>

int main() {
	char *cat = base64((byte *)"cat", 3);
	char *ca = base64((byte *)"ca", 2);
	char *c = base64((byte *)"c", 1);
	assert(cat);
	assert(ca);
	assert(c);
	assert(!strcmp("Y2F0", cat));
	assert(!strcmp("Y2E=", ca));
	assert(!strcmp("Yw==", c));
	free(cat);
	free(ca);
	free(c);

	char *fzf = base64((byte []) { 0xFF, 0x00, 0xFF }, 3);
	char *zfz = base64((byte []) { 0x00, 0xFF, 0x00 }, 3);
	assert(fzf);
	assert(zfz);
	assert(!strcmp("/wD/", fzf));
	assert(!strcmp("AP8A", zfz));
	free(fzf);
	free(zfz);
}

#endif
