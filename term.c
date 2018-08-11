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
#include <stdio.h>
#include <stdlib.h>

#include "chat.h"

#define PAIR(a, b) (((short)(a) << 8) | ((short)(b) & 0xFF))

static void privateMode(const char *mode, bool set) {
	printf("\33[?%s%c", mode, (set ? 'h' : 'l'));
	fflush(stdout);
}

void termMode(enum TermMode mode, bool set) {
	switch (mode) {
		break; case TERM_FOCUS: privateMode("1004", set);
		break; case TERM_PASTE: privateMode("2004", set);
	}
}

enum TermEvent termEvent(char ch) {
	static char state = '\0';
	switch (PAIR(state, ch)) {
		break; case PAIR('\0', '\33'): state = '\33';
		break; case PAIR('\33', '['):  state = '[';
		break; case PAIR('[', 'I'):    state = '\0'; return TERM_FOCUS_IN;
		break; case PAIR('[', 'O'):    state = '\0'; return TERM_FOCUS_OUT;
		break; case PAIR('[', '2'):    state = '2';
		break; case PAIR('2', '0'):    state = '0';
		break; case PAIR('0', '0'):    state = '0';
		break; case PAIR('0', '~'):    state = '\0'; return TERM_PASTE_START;
		break; case PAIR('0', '1'):    state = '1';
		break; case PAIR('1', '~'):    state = '\0'; return TERM_PASTE_END;
		break; default:                state = '\0';
	}
	return TERM_NONE;
}
