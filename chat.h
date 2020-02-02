/* Copyright (C) 2020  C. McEnroe <june@causal.agency>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <err.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sysexits.h>
#include <time.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof(a[0]))
#define BIT(x) x##Bit, x = 1 << x##Bit, x##Bit_ = x##Bit

typedef unsigned char byte;

enum Color {
	White, Black, Blue, Green, Red, Brown, Magenta, Orange,
	Yellow, LightGreen, Cyan, LightCyan, LightBlue, Pink, Gray, LightGray,
	Default = 99,
};

enum { None, Debug, Network, IDCap = 256 };
extern char *idNames[IDCap];
extern enum Color idColors[IDCap];
extern size_t idNext;

static inline size_t idFind(const char *name) {
	for (size_t id = 0; id < idNext; ++id) {
		if (!strcmp(idNames[id], name)) return id;
	}
	return None;
}

static inline size_t idFor(const char *name) {
	size_t id = idFind(name);
	if (id) return id;
	idNames[idNext] = strdup(name);
	if (!idNames[idNext]) err(EX_OSERR, "strdup");
	return idNext++;
}

#define ENUM_CAP \
	X("sasl", CapSASL) \
	X("server-time", CapServerTime) \
	X("userhost-in-names", CapUserhostInNames)

enum Cap {
#define X(name, id) BIT(id),
	ENUM_CAP
#undef X
};

extern struct Self {
	bool debug;
	char *plain;
	const char *join;
	enum Cap caps;
	char *network;
	char *chanTypes;
	char *prefixes;
	char *nick;
} self;

static inline void set(char **field, const char *value) {
	free(*field);
	*field = strdup(value);
	if (!*field) err(EX_OSERR, "strdup");
}

#define ENUM_TAG \
	X("time", TagTime)

enum Tag {
#define X(name, id) id,
	ENUM_TAG
#undef X
	TagCap,
};

enum { ParamCap = 15 };
struct Message {
	char *tags[TagCap];
	char *nick;
	char *user;
	char *host;
	char *cmd;
	char *params[ParamCap];
};

void ircConfig(bool insecure, const char *cert, const char *priv);
int ircConnect(const char *host, const char *port);
void ircRecv(void);
void ircSend(const char *ptr, size_t len);
void ircFormat(const char *format, ...)
	__attribute__((format(printf, 1, 2)));

void handle(struct Message msg);

enum Heat { Cold, Warm, Hot };
void uiInit(void);
void uiDraw(void);
void uiShowID(size_t id);
void uiWrite(size_t id, enum Heat heat, const struct tm *time, const char *str);
void uiFormat(
	size_t id, enum Heat heat, const struct tm *time, const char *format, ...
) __attribute__((format(printf, 4, 5)));

enum TermMode {
	TermFocus,
	TermPaste,
};
enum TermEvent {
	TermNone,
	TermFocusIn,
	TermFocusOut,
	TermPasteStart,
	TermPasteEnd,
};
void termInit(void);
void termNoFlow(void);
void termTitle(const char *title);
void termMode(enum TermMode mode, bool set);
enum TermEvent termEvent(char ch);

static inline enum Color hash(const char *str) {
	if (*str == '~') str++;
	uint32_t hash = 0;
	for (; *str; ++str) {
		hash = (hash << 5) | (hash >> 27);
		hash ^= *str;
		hash *= 0x27220A95;
	}
	return 2 + hash % 14;
}

#define BASE64_SIZE(len) (1 + ((len) + 2) / 3 * 4)
static const char Base64[64] = {
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
};
static inline void base64(char *dst, const byte *src, size_t len) {
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

// Defined in libcrypto if missing from libc:
void explicit_bzero(void *b, size_t len);
