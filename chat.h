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
#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sysexits.h>
#include <time.h>
#include <wchar.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof(a[0]))
#define BIT(x) x##Bit, x = 1 << x##Bit, x##Bit_ = x##Bit

typedef unsigned char byte;

enum Color {
	White, Black, Blue, Green, Red, Brown, Magenta, Orange,
	Yellow, LightGreen, Cyan, LightCyan, LightBlue, Pink, Gray, LightGray,
	Default = 99,
	ColorCap,
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
	if (idNext == IDCap) return Network;
	idNames[idNext] = strdup(name);
	if (!idNames[idNext]) err(EX_OSERR, "strdup");
	idColors[idNext] = Default;
	return idNext++;
}

extern uint32_t hashInit;
static inline enum Color hash(const char *str) {
	if (*str == '~') str++;
	uint32_t hash = hashInit;
	for (; *str; ++str) {
		hash = (hash << 5) | (hash >> 27);
		hash ^= *str;
		hash *= 0x27220A95;
	}
	return 2 + hash % 74;
}

#define ENUM_CAP \
	X("extended-join", CapExtendedJoin) \
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
	bool restricted;
	char *plain;
	const char *join;
	enum Cap caps;
	char *network;
	char *chanTypes;
	char *prefixes;
	char *nick;
	char *user;
	enum Color color;
	char *quit;
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

void ircConfig(bool insecure, FILE *cert, FILE *priv);
int ircConnect(const char *bind, const char *host, const char *port);
void ircRecv(void);
void ircSend(const char *ptr, size_t len);
void ircFormat(const char *format, ...)
	__attribute__((format(printf, 1, 2)));
void ircClose(void);

extern struct Replies {
	size_t join;
	size_t list;
	size_t names;
	size_t topic;
	size_t whois;
} replies;

void handle(struct Message msg);
void command(size_t id, char *input);
const char *commandIsPrivmsg(size_t id, const char *input);
const char *commandIsNotice(size_t id, const char *input);
const char *commandIsAction(size_t id, const char *input);
void commandComplete(void);

int utilPipe[2];

enum { UtilCap = 16 };
struct Util {
	size_t argc;
	const char *argv[UtilCap];
};

static inline void utilPush(struct Util *util, const char *arg) {
	if (1 + util->argc < UtilCap) {
		util->argv[util->argc++] = arg;
	} else {
		errx(EX_CONFIG, "too many utility arguments");
	}
}

enum Heat { Cold, Warm, Hot };
extern struct Util uiNotifyUtil;
void uiInit(void);
void uiShow(void);
void uiHide(void);
void uiDraw(void);
void uiShowID(size_t id);
void uiShowNum(size_t num);
void uiMoveID(size_t id, size_t num);
void uiCloseID(size_t id);
void uiCloseNum(size_t id);
void uiRead(void);
void uiWrite(size_t id, enum Heat heat, const time_t *time, const char *str);
void uiFormat(
	size_t id, enum Heat heat, const time_t *time, const char *format, ...
) __attribute__((format(printf, 4, 5)));
void uiLoad(const char *name);
int uiSave(const char *name);

enum Edit {
	EditHead,
	EditTail,
	EditPrev,
	EditNext,
	EditPrevWord,
	EditNextWord,
	EditDeleteHead,
	EditDeleteTail,
	EditDeletePrev,
	EditDeleteNext,
	EditDeletePrevWord,
	EditDeleteNextWord,
	EditPaste,
	EditTranspose,
	EditInsert,
	EditComplete,
	EditEnter,
};
void edit(size_t id, enum Edit op, wchar_t ch);
char *editBuffer(size_t *pos);

const char *complete(size_t id, const char *prefix);
void completeAccept(void);
void completeReject(void);
void completeAdd(size_t id, const char *str, enum Color color);
void completeTouch(size_t id, const char *str, enum Color color);
void completeReplace(size_t id, const char *old, const char *new);
void completeRemove(size_t id, const char *str);
void completeClear(size_t id);
size_t completeID(const char *str);
enum Color completeColor(size_t id, const char *str);

extern struct Util urlOpenUtil;
extern struct Util urlCopyUtil;
void urlScan(size_t id, const char *nick, const char *mesg);
void urlOpenCount(size_t id, size_t count);
void urlOpenMatch(size_t id, const char *str);
void urlCopyMatch(size_t id, const char *str);

FILE *configOpen(const char *path, const char *mode);
FILE *dataOpen(const char *path, const char *mode);

int getopt_config(
	int argc, char *const *argv,
	const char *optstring, const struct option *longopts, int *longindex
);

// Defined in libcrypto if missing from libc:
void explicit_bzero(void *b, size_t len);
#ifndef strlcat
size_t strlcat(char *restrict dst, const char *restrict src, size_t dstsize);
#endif
