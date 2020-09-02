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
 *
 * Additional permission under GNU GPL version 3 section 7:
 *
 * If you modify this Program, or any covered work, by linking or
 * combining it with OpenSSL (or a modified version of that library),
 * containing parts covered by the terms of the OpenSSL License and the
 * original SSLeay license, the licensors of this Program grant you
 * additional permission to convey the resulting work. Corresponding
 * Source for a non-source form of such a combination shall include the
 * source code for the parts of OpenSSL used as well as that of the
 * covered work.
 */

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>
#include <time.h>
#include <wchar.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof(a[0]))
#define BIT(x) x##Bit, x = 1 << x##Bit, x##Bit_ = x##Bit

typedef unsigned uint;
typedef unsigned char byte;

struct Cat {
	char *buf;
	size_t cap;
	size_t len;
};
static inline void __attribute__((format(printf, 2, 3)))
catf(struct Cat *cat, const char *format, ...) {
	va_list ap;
	va_start(ap, format);
	int len = vsnprintf(&cat->buf[cat->len], cat->cap - cat->len, format, ap);
	assert(len >= 0);
	va_end(ap);
	cat->len += len;
	if (cat->len >= cat->cap) cat->len = cat->cap - 1;
}

enum Attr {
	BIT(Bold),
	BIT(Reverse),
	BIT(Italic),
	BIT(Underline),
};
enum Color {
	White, Black, Blue, Green, Red, Brown, Magenta, Orange,
	Yellow, LightGreen, Cyan, LightCyan, LightBlue, Pink, Gray, LightGray,
	Default = 99,
	ColorCap,
};
struct Style {
	enum Attr attr;
	enum Color fg, bg;
};

static const struct Style StyleDefault = { 0, Default, Default };
enum { B = '\2', C = '\3', O = '\17', R = '\26', I = '\35', U = '\37' };

static inline size_t styleParse(struct Style *style, const char **str) {
	switch (**str) {
		break; case B: (*str)++; style->attr ^= Bold;
		break; case O: (*str)++; *style = StyleDefault;
		break; case R: (*str)++; style->attr ^= Reverse;
		break; case I: (*str)++; style->attr ^= Italic;
		break; case U: (*str)++; style->attr ^= Underline;
		break; case C: {
			(*str)++;
			if (!isdigit(**str)) {
				style->fg = Default;
				style->bg = Default;
				break;
			}
			style->fg = *(*str)++ - '0';
			if (isdigit(**str)) style->fg = style->fg * 10 + *(*str)++ - '0';
			if ((*str)[0] != ',' || !isdigit((*str)[1])) break;
			(*str)++;
			style->bg = *(*str)++ - '0';
			if (isdigit(**str)) style->bg = style->bg * 10 + *(*str)++ - '0';
		}
	}
	return strcspn(*str, (const char[]) { B, C, O, R, I, U, '\0' });
}

enum { None, Debug, Network, IDCap = 256 };
extern char *idNames[IDCap];
extern enum Color idColors[IDCap];
extern uint idNext;

static inline uint idFind(const char *name) {
	for (uint id = 0; id < idNext; ++id) {
		if (!strcmp(idNames[id], name)) return id;
	}
	return None;
}

static inline uint idFor(const char *name) {
	uint id = idFind(name);
	if (id) return id;
	if (idNext == IDCap) return Network;
	idNames[idNext] = strdup(name);
	idColors[idNext] = Default;
	if (!idNames[idNext]) err(EX_OSERR, "strdup");
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
	return Blue + hash % 74;
}

extern struct Network {
	char *name;
	uint userLen;
	uint hostLen;
	char *chanTypes;
	char *prefixes;
	char *prefixModes;
	char *listModes;
	char *paramModes;
	char *setParamModes;
	char *channelModes;
	char excepts;
	char invex;
} network;

#define ENUM_CAP \
	X("causal.agency/consumer", CapConsumer) \
	X("chghost", CapChghost) \
	X("extended-join", CapExtendedJoin) \
	X("invite-notify", CapInviteNotify) \
	X("multi-prefix", CapMultiPrefix) \
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
	size_t pos;
	enum Cap caps;
	char *plain;
	char *join;
	char *nick;
	char *user;
	char *host;
	enum Color color;
	char *quit;
} self;

static inline void set(char **field, const char *value) {
	free(*field);
	*field = strdup(value);
	if (!*field) err(EX_OSERR, "strdup");
}

#define ENUM_TAG \
	X("causal.agency/pos", TagPos) \
	X("time", TagTime)

enum Tag {
#define X(name, id) id,
	ENUM_TAG
#undef X
	TagCap,
};

enum { ParamCap = 254 };
struct Message {
	char *tags[TagCap];
	char *nick;
	char *user;
	char *host;
	char *cmd;
	char *params[ParamCap];
};

void ircConfig(bool insecure, const char *cert, const char *priv);
int ircConnect(const char *bind, const char *host, const char *port);
void ircRecv(void);
void ircSend(const char *ptr, size_t len);
void ircFormat(const char *format, ...)
	__attribute__((format(printf, 1, 2)));
void ircClose(void);

extern uint execID;
extern int execPipe[2];
extern int utilPipe[2];

enum { UtilCap = 16 };
struct Util {
	uint argc;
	const char *argv[UtilCap];
};

static inline void utilPush(struct Util *util, const char *arg) {
	if (1 + util->argc < UtilCap) {
		util->argv[util->argc++] = arg;
	} else {
		errx(EX_CONFIG, "too many utility arguments");
	}
}

extern struct Replies {
	uint away;
	uint ban;
	uint excepts;
	uint invex;
	uint join;
	uint list;
	uint mode;
	uint names;
	uint topic;
	uint whois;
} replies;

void handle(struct Message *msg);
void command(uint id, char *input);
const char *commandIsPrivmsg(uint id, const char *input);
const char *commandIsNotice(uint id, const char *input);
const char *commandIsAction(uint id, const char *input);
void commandCompleteAdd(void);

enum Heat { Ice, Cold, Warm, Hot };
extern struct Util uiNotifyUtil;
void uiInit(void);
void uiShow(void);
void uiHide(void);
void uiDraw(void);
void uiShowID(uint id);
void uiShowNum(uint num);
void uiMoveID(uint id, uint num);
void uiCloseID(uint id);
void uiCloseNum(uint id);
void uiRead(void);
void uiWrite(uint id, enum Heat heat, const time_t *time, const char *str);
void uiFormat(
	uint id, enum Heat heat, const time_t *time, const char *format, ...
) __attribute__((format(printf, 4, 5)));
void uiLoad(const char *name);
int uiSave(const char *name);

enum { BufferCap = 1024 };
struct Buffer;
struct Line {
	enum Heat heat;
	time_t time;
	char *str;
};
struct Buffer *bufferAlloc(void);
void bufferFree(struct Buffer *buffer);
const struct Line *bufferSoft(const struct Buffer *buffer, size_t i);
const struct Line *bufferHard(const struct Buffer *buffer, size_t i);
void bufferPush(
	struct Buffer *buffer, int cols,
	enum Heat heat, time_t time, const char *str
);
void bufferReflow(struct Buffer *buffer, int cols);

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
	EditCollapse,
	EditInsert,
	EditComplete,
	EditExpand,
	EditEnter,
};
void edit(uint id, enum Edit op, wchar_t ch);
char *editBuffer(size_t *pos);
void editCompleteAdd(void);

const char *complete(uint id, const char *prefix);
void completeAccept(void);
void completeReject(void);
void completeAdd(uint id, const char *str, enum Color color);
void completeTouch(uint id, const char *str, enum Color color);
void completeReplace(uint id, const char *old, const char *new);
void completeRemove(uint id, const char *str);
void completeClear(uint id);
uint completeID(const char *str);
enum Color completeColor(uint id, const char *str);

extern struct Util urlOpenUtil;
extern struct Util urlCopyUtil;
void urlScan(uint id, const char *nick, const char *mesg);
void urlOpenCount(uint id, uint count);
void urlOpenMatch(uint id, const char *str);
void urlCopyMatch(uint id, const char *str);

enum { IgnoreCap = 256 };
extern struct Ignore {
	size_t len;
	char *patterns[IgnoreCap];
} ignore;
const char *ignoreAdd(const char *pattern);
bool ignoreRemove(const char *pattern);
enum Heat ignoreCheck(enum Heat heat, uint id, const struct Message *msg);

extern bool logEnable;
void logFormat(uint id, const time_t *time, const char *format, ...)
	__attribute__((format(printf, 3, 4)));
void logClose(void);

const char *configPath(const char **dirs, const char *path);
const char *dataPath(const char **dirs, const char *path);
FILE *configOpen(const char *path, const char *mode);
FILE *dataOpen(const char *path, const char *mode);
void dataMkdir(const char *path);

int getopt_config(
	int argc, char *const *argv,
	const char *optstring, const struct option *longopts, int *longindex
);
