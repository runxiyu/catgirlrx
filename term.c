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

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "chat.h"

static bool xterm;

void termInit(void) {
	char *term = getenv("TERM");
	xterm = term && !strncmp(term, "xterm", 5);
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

#define T(s, i) ((s) << 8 | (i))

enum { Esc = '\33' };

enum TermEvent termEvent(char ch) {
	static uint state = 0;
	switch (T(state, ch)) {
		case T(0, Esc): state = 1; return 0;
		case T(1, '['): state = 2; return 0;
		case T(2, 'I'): state = 0; return TermFocusIn;
		case T(2, 'O'): state = 0; return TermFocusOut;
		case T(2, '2'): state = 3; return 0;
		case T(3, '0'): state = 4; return 0;
		case T(4, '0'): state = 5; return 0;
		case T(5, '~'): state = 0; return TermPasteStart;
		case T(4, '1'): state = 6; return 0;
		case T(6, '~'): state = 0; return TermPasteEnd;
		default:        state = 0; return 0;
	}
}

#ifdef TEST
#include <assert.h>

static bool testEvent(const char *str, enum TermEvent event) {
	enum TermEvent e = TermNone;
	for (size_t i = 0; i < strlen(str); ++i) {
		if (e) return false;
		e = termEvent(str[i]);
	}
	return (e == event);
}

int main() {
	assert(testEvent("\33[I", TermFocusIn));
	assert(testEvent("\33[O", TermFocusOut));
	assert(testEvent("\33[200~", TermPasteStart));
	assert(testEvent("\33[201~", TermPasteEnd));
}

#endif
