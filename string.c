/* Copyright (C) 2018  C. McEnroe <june@causal.agency>
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

#include <err.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

#include "chat.h"

static const char Base64[64] = {
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
};

size_t base64Size(size_t len) {
	return 1 + (len + 2) / 3 * 4;
}

void base64(char *dst, const byte *src, size_t len) {
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
}

#ifdef TEST
#include <assert.h>

int main() {
	assert(5 == base64Size(1));
	assert(5 == base64Size(2));
	assert(5 == base64Size(3));
	assert(9 == base64Size(4));

	char b64[base64Size(3)];
	assert((base64(b64, (byte *)"cat", 3), !strcmp("Y2F0", b64)));
	assert((base64(b64, (byte *)"ca", 2), !strcmp("Y2E=", b64)));
	assert((base64(b64, (byte *)"c", 1), !strcmp("Yw==", b64)));

	assert((base64(b64, (byte *)"\xFF\x00\xFF", 3), !strcmp("/wD/", b64)));
	assert((base64(b64, (byte *)"\x00\xFF\x00", 3), !strcmp("AP8A", b64)));
}

#endif
