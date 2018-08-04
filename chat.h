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
void uiTopic(const char *topic);
void uiChat(const char *line);

__attribute__((format(printf, 1, 2)))
void uiFmt(const char *format, ...);

void handle(char *line);
void input(wchar_t *line);
