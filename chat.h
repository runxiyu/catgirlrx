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

#define SOURCE_URL "https://code.causal.agency/june/catgirl"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <time.h>
#include <wchar.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define err(...) do { uiHide(); err(__VA_ARGS__); } while (0)
#define errx(...) do { uiHide(); errx(__VA_ARGS__); } while (0)

typedef unsigned uint;
typedef unsigned char byte;

struct {
	char *host;
	char *port;
	char *auth;
	char *pass;
	char *webp;
	char *nick;
	char *user;
	char *real;
	char *join;
	bool raw;
	bool notify;
	bool quit;
} self;

void eventWait(const char *argv[static 2]);
void eventPipe(const char *argv[static 2]);
noreturn void eventLoop(void);

enum IRCColor {
	IRCWhite,
	IRCBlack,
	IRCBlue,
	IRCGreen,
	IRCRed,
	IRCBrown,
	IRCMagenta,
	IRCOrange,
	IRCYellow,
	IRCLightGreen,
	IRCCyan,
	IRCLightCyan,
	IRCLightBlue,
	IRCPink,
	IRCGray,
	IRCLightGray,
	IRCDefault = 99,
};
enum {
	IRCBold      = 002,
	IRCColor     = 003,
	IRCReverse   = 026,
	IRCReset     = 017,
	IRCItalic    = 035,
	IRCUnderline = 037,
};

struct Tag {
	size_t id;
	const char *name;
	enum IRCColor color;
};

enum { TagsLen = 256 };
const struct Tag TagNone;
const struct Tag TagStatus;
const struct Tag TagRaw;
struct Tag tagFind(const char *name);
struct Tag tagFor(const char *name, enum IRCColor color);

struct Format {
	const wchar_t *str;
	size_t len;
	bool split;
	bool bold, italic, underline, reverse;
	enum IRCColor fg, bg;
};
void formatReset(struct Format *format);
bool formatParse(struct Format *format, const wchar_t *split);
enum IRCColor formatColor(const char *str);

void handle(char *line);
void input(struct Tag tag, char *line);
void inputTab(void);

int ircConnect(void);
void ircRead(void);
void ircWrite(const char *ptr, size_t len);
void ircFmt(const char *format, ...) __attribute__((format(printf, 1, 2)));
void ircQuit(const char *mesg);

void uiInit(void);
void uiShow(void);
void uiHide(void);
void uiDraw(void);
void uiRead(void);
void uiExit(int status);

void uiPrompt(bool nickChanged);
void uiShowTag(struct Tag tag);
void uiShowNum(int num, bool relative);
void uiCloseTag(struct Tag tag);

enum UIHeat {
	UICold,
	UIWarm,
	UIHot,
};
void uiLog(struct Tag tag, enum UIHeat heat, const wchar_t *str);
void uiFmt(struct Tag tag, enum UIHeat heat, const wchar_t *format, ...);

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

enum Edit {
	EditLeft,
	EditRight,
	EditHome,
	EditEnd,
	EditBackWord,
	EditForeWord,
	EditInsert,
	EditBackspace,
	EditDelete,
	EditKillBackWord,
	EditKillForeWord,
	EditKillLine,
	EditComplete,
	EditEnter,
};
void edit(struct Tag tag, enum Edit op, wchar_t ch);
const wchar_t *editHead(void);
const wchar_t *editTail(void);

void tabTouch(struct Tag tag, const char *word);
void tabAdd(struct Tag tag, const char *word);
void tabRemove(struct Tag tag, const char *word);
void tabReplace(struct Tag tag, const char *prev, const char *next);
void tabClear(struct Tag tag);
struct Tag tabTag(const char *word);
const char *tabNext(struct Tag tag, const char *prefix);
void tabAccept(void);
void tabReject(void);

void urlScan(struct Tag tag, const char *str);
void urlList(struct Tag tag);
void urlOpenMatch(struct Tag tag, const char *substr);
void urlOpenRange(struct Tag tag, size_t at, size_t to);

void logOpen(const char *path);
void logFmt(
	struct Tag tag, const time_t *ts, const char *format, ...
) __attribute__((format(printf, 3, 4)));
void logList(struct Tag tag);
void logReplay(struct Tag tag);

wchar_t *wcsnchr(const wchar_t *wcs, size_t len, wchar_t chr);
wchar_t *wcsnrchr(const wchar_t *wcs, size_t len, wchar_t chr);
wchar_t *ambstowcs(const char *src);
char *awcstombs(const wchar_t *src);
char *awcsntombs(const wchar_t *src, size_t nwc);
int vaswprintf(wchar_t **ret, const wchar_t *format, va_list ap);
int aswprintf(wchar_t **ret, const wchar_t *format, ...);

size_t base64Size(size_t len);
void base64(char *dst, const byte *src, size_t len);

// HACK: clang won't check wchar_t *format strings.
#ifdef NDEBUG
#define uiFmt(tag, heat, format, ...) uiFmt(tag, heat, L##format, __VA_ARGS__)
#else
#define uiFmt(tag, heat, format, ...) do { \
	snprintf(NULL, 0, format, __VA_ARGS__); \
	uiFmt(tag, heat, L##format, __VA_ARGS__); \
} while(0)
#endif
