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

#define SOURCE_URL "https://code.causal.agency/june/chat"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#define err(...) do { uiHide(); err(__VA_ARGS__); } while (0)
#define errx(...) do { uiHide(); errx(__VA_ARGS__); } while (0)

struct {
	bool verbose;
	char *nick;
	char *user;
	char *join;
} self;

void selfNick(const char *nick);
void selfUser(const char *user);
void selfJoin(const char *join);

struct Tag {
	size_t id;
	const char *name;
};

enum { TAGS_LEN = 256 };
const struct Tag TAG_NONE;
const struct Tag TAG_STATUS;
const struct Tag TAG_VERBOSE;
struct Tag tagFind(const char *name);
struct Tag tagFor(const char *name);

enum {
	IRC_WHITE,
	IRC_BLACK,
	IRC_BLUE,
	IRC_GREEN,
	IRC_RED,
	IRC_BROWN,
	IRC_MAGENTA,
	IRC_ORANGE,
	IRC_YELLOW,
	IRC_LIGHT_GREEN,
	IRC_CYAN,
	IRC_LIGHT_CYAN,
	IRC_LIGHT_BLUE,
	IRC_PINK,
	IRC_GRAY,
	IRC_LIGHT_GRAY,
};
enum {
	IRC_BOLD      = 002,
	IRC_COLOR     = 003,
	IRC_REVERSE   = 026,
	IRC_RESET     = 017,
	IRC_ITALIC    = 035,
	IRC_UNDERLINE = 037,
};

void handle(char *line);
void input(struct Tag tag, char *line);
void inputTab(void);

int ircConnect(
	const char *host, const char *port, const char *pass, const char *webPass
);
void ircRead(void);
void ircWrite(const char *ptr, size_t len);
void ircFmt(const char *format, ...) __attribute__((format(printf, 1, 2)));

void uiInit(void);
void uiHide(void);
void uiExit(void);
void uiDraw(void);
void uiRead(void);
void uiViewTag(struct Tag tag);
void uiViewNum(int num);
void uiTopic(struct Tag tag, const char *topic);
void uiLog(struct Tag tag, const wchar_t *line);
void uiFmt(struct Tag tag, const wchar_t *format, ...);

enum TermMode {
	TERM_FOCUS,
	TERM_PASTE,
};
enum TermEvent {
	TERM_NONE,
	TERM_FOCUS_IN,
	TERM_FOCUS_OUT,
	TERM_PASTE_START,
	TERM_PASTE_END,
};
void termMode(enum TermMode mode, bool set);
enum TermEvent termEvent(char ch);

enum Edit {
	EDIT_LEFT,
	EDIT_RIGHT,
	EDIT_HOME,
	EDIT_END,
	EDIT_BACK_WORD,
	EDIT_FORE_WORD,
	EDIT_INSERT,
	EDIT_BACKSPACE,
	EDIT_DELETE,
	EDIT_KILL_BACK_WORD,
	EDIT_KILL_FORE_WORD,
	EDIT_KILL_LINE,
	EDIT_COMPLETE,
	EDIT_ENTER,
};
void edit(struct Tag tag, enum Edit op, wchar_t ch);
const wchar_t *editHead(void);
const wchar_t *editTail(void);

void tabTouch(struct Tag tag, const char *word);
void tabRemove(struct Tag tag, const char *word);
void tabReplace(struct Tag tag, const char *prev, const char *next);
void tabClear(struct Tag tag);
struct Tag tabTag(const char *word);
const char *tabNext(struct Tag tag, const char *prefix);
void tabAccept(void);
void tabReject(void);

void urlScan(struct Tag tag, const char *str);
void urlList(struct Tag tag);
void urlOpen(struct Tag tag, size_t at, size_t to);

void spawn(char *const argv[]);

wchar_t *ambstowcs(const char *src);
char *awcstombs(const wchar_t *src);
int vaswprintf(wchar_t **ret, const wchar_t *format, va_list ap);

// HACK: clang won't check wchar_t *format strings.
#ifdef NDEBUG
#define uiFmt(tag, format, ...) uiFmt(tag, L##format, __VA_ARGS__)
#else
#define uiFmt(tag, format, ...) do { \
	snprintf(NULL, 0, format, __VA_ARGS__); \
	uiFmt(tag, L##format, __VA_ARGS__); \
} while(0)
#endif
