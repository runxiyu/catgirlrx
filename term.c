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

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#include "chat.h"

static bool xterm;

void termInit(void) {
	const char *term = getenv("TERM");
	xterm = (term && !strncmp(term, "xterm", 5));
}

void termNoFlow(void) {
	struct termios attr;
	int error = tcgetattr(STDIN_FILENO, &attr);
	if (error) return;
	attr.c_iflag &= ~IXON;
	attr.c_cc[VDISCARD] = _POSIX_VDISABLE;
	tcsetattr(STDIN_FILENO, TCSANOW, &attr);
}

void termTitle(const char *title) {
	if (!xterm) return;
	printf("\33]0;%s\33\\", title);
	fflush(stdout);
}

static void privateMode(const char *mode, bool set) {
	printf("\33[?%s%c", mode, (set ? 'h' : 'l'));
	fflush(stdout);
}

void termMode(enum TermMode mode, bool set) {
	switch (mode) {
		break; case TermFocus: privateMode("1004", set);
		break; case TermPaste: privateMode("2004", set);
	}
}

enum { Esc = '\33' };

enum TermEvent termEvent(char ch) {
	static int st;
#define T(st, ch) ((st) << 8 | (ch))
	switch (T(st, ch)) {
		break; case T(0, Esc): st = 1;
		break; case T(1, '['): st = 2;
		break; case T(2, 'I'): st = 0; return TermFocusIn;
		break; case T(2, 'O'): st = 0; return TermFocusOut;
		break; case T(2, '2'): st = 3;
		break; case T(3, '0'): st = 4;
		break; case T(4, '0'): st = 5;
		break; case T(5, '~'): st = 0; return TermPasteStart;
		break; case T(4, '1'): st = 6;
		break; case T(6, '~'): st = 0; return TermPasteEnd;
		break; default: st = 0;
	}
	return 0;
#undef T
}
