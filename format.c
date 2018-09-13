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

#include <wchar.h>
#include <stdlib.h>
#include <stdbool.h>

#include "chat.h"

void formatReset(struct Format *format) {
	format->bold = false;
	format->italic = false;
	format->underline = false;
	format->reverse = false;
	format->fg = -1;
	format->bg = -1;
}

static void parseColor(struct Format *format) {
	size_t len = MIN(wcsspn(format->str, L"0123456789"), 2);
	if (!len) {
		format->fg = -1;
		format->bg = -1;
		return;
	}
	format->fg = 0;
	for (size_t i = 0; i < len; ++i) {
		format->fg *= 10;
		format->fg += format->str[i] - L'0';
	}
	if (format->fg > IRCLightGray) format->fg = -1;
	format->str = &format->str[len];

	len = 0;
	if (format->str[0] == L',') {
		len = MIN(wcsspn(&format->str[1], L"0123456789"), 2);
	}
	if (!len) return;
	format->bg = 0;
	for (size_t i = 0; i < len; ++i) {
		format->bg *= 10;
		format->bg += format->str[1 + i] - L'0';
	}
	if (format->bg > IRCLightGray) format->bg = -1;
	format->str = &format->str[1 + len];
}

static const wchar_t Stops[] = {
	L' ',
	IRCBold, IRCColor, IRCReverse, IRCReset, IRCItalic, IRCUnderline,
	L'\0',
};

bool formatParse(struct Format *format, const wchar_t *split) {
	format->str += format->len;
	if (!format->str[0]) return false;

	const wchar_t *init = format->str;
	switch (format->str[0]) {
		break; case IRCBold:      format->str++; format->bold ^= true;
		break; case IRCItalic:    format->str++; format->italic ^= true;
		break; case IRCUnderline: format->str++; format->underline ^= true;
		break; case IRCReverse:   format->str++; format->reverse ^= true;
		break; case IRCColor:     format->str++; parseColor(format);
		break; case IRCReset:     format->str++; formatReset(format);
	}
	format->split = (split >= init && split <= format->str);

	if (format->str[0] == L' ') {
		format->len = 1 + wcscspn(&format->str[1], Stops);
	} else {
		format->len = wcscspn(format->str, Stops);
	}
	if (split > format->str && split < &format->str[format->len]) {
		format->len = split - format->str;
	}
	return true;
}
