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
#include <stdlib.h>
#include <wchar.h>

#define err(...) do { uiHide(); err(__VA_ARGS__); } while (0)
#define errx(...) do { uiHide(); errx(__VA_ARGS__); } while (0)

struct {
	bool verbose;
	char *nick;
	char *user;
	char *chan;
} chat;

int ircConnect(const char *host, const char *port, const char *webPass);
void ircRead(void);
void ircWrite(const char *ptr, size_t len);

__attribute__((format(printf, 1, 2)))
void ircFmt(const char *format, ...);

void uiInit(void);
void uiHide(void);
void uiDraw(void);
void uiRead(void);
void uiTopic(const wchar_t *topic);
void uiTopicStr(const char *topic);
void uiLog(const wchar_t *line);
void uiFmt(const wchar_t *format, ...);

// HACK: clang won't check wchar_t *format strings.
#ifdef NDEBUG
#define uiFmt(format, ...) uiFmt(L##format, __VA_ARGS__)
#else
#define uiFmt(format, ...) do { \
	snprintf(NULL, 0, format, __VA_ARGS__); \
	uiFmt(L##format, __VA_ARGS__); \
} while(0)
#endif

void handle(char *line);
void input(char *line);

void tabTouch(const char *word);
void tabRemove(const char *word);
void tabReplace(const char *prev, const char *next);

wchar_t *ambstowcs(const char *src);
char *awcstombs(const wchar_t *src);
int vaswprintf(wchar_t **ret, const wchar_t *format, va_list ap);
