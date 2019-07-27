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
#include <stdint.h>
#include <stdlib.h>
#include <wchar.h>

#include "chat.h"

void formatReset(struct Format *format) {
	format->bold = false;
	format->italic = false;
	format->underline = false;
	format->reverse = false;
	format->fg = IRCDefault;
	format->bg = IRCDefault;
}

static void parseColor(struct Format *format) {
	size_t len = MIN(wcsspn(format->str, L"0123456789"), 2);
	if (!len) {
		format->fg = IRCDefault;
		format->bg = IRCDefault;
		return;
	}
	format->fg = 0;
	for (size_t i = 0; i < len; ++i) {
		format->fg *= 10;
		format->fg += format->str[i] - L'0';
	}
	if (format->fg > IRCLightGray) format->fg = IRCDefault;
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
	if (format->bg > IRCLightGray) format->bg = IRCDefault;
	format->str = &format->str[1 + len];
}

static const wchar_t Codes[] = {
	IRCBold, IRCColor, IRCReverse, IRCReset, IRCItalic, IRCUnderline, L'\0',
};

bool formatParse(struct Format *format, const wchar_t *split) {
	format->str += format->len;
	if (!format->str[0]) {
		if (split == format->str && !format->split) {
			format->len = 0;
			format->split = true;
			return true;
		}
		return false;
	}

	const wchar_t *init = format->str;
	for (bool done = false; !done;) {
		switch (format->str[0]) {
			break; case IRCBold:      format->str++; format->bold ^= true;
			break; case IRCItalic:    format->str++; format->italic ^= true;
			break; case IRCUnderline: format->str++; format->underline ^= true;
			break; case IRCReverse:   format->str++; format->reverse ^= true;
			break; case IRCColor:     format->str++; parseColor(format);
			break; case IRCReset:     format->str++; formatReset(format);
			break; default:           done = true;
		}
	}
	format->split = (split >= init && split <= format->str);

	format->len = wcscspn(format->str, Codes);
	if (split > format->str && split < &format->str[format->len]) {
		format->len = split - format->str;
	}
	return true;
}

#ifdef TEST
#include <assert.h>

static bool testColor(
	const wchar_t *str, enum IRCColor fg, enum IRCColor bg, size_t index
) {
	struct Format format = { .str = str };
	formatReset(&format);
	if (!formatParse(&format, NULL)) return false;
	if (format.fg != fg) return false;
	if (format.bg != bg) return false;
	return (format.str == &str[index]);
}

static bool testSplit(const wchar_t *str, size_t index) {
	struct Format format = { .str = str };
	formatReset(&format);
	bool split = false;
	while (formatParse(&format, &str[index])) {
		if (format.split && split) return false;
		if (format.split) split = true;
	}
	return split;
}

static bool testSplits(const wchar_t *str) {
	for (size_t i = 0; i <= wcslen(str); ++i) {
		if (!testSplit(str, i)) return false;
	}
	return true;
}

int main() {
	assert(testColor(L"\003a",      IRCDefault,   IRCDefault,   1));
	assert(testColor(L"\003,a",     IRCDefault,   IRCDefault,   1));
	assert(testColor(L"\003,1",     IRCDefault,   IRCDefault,   1));
	assert(testColor(L"\0031a",     IRCBlack,     IRCDefault,   2));
	assert(testColor(L"\0031,a",    IRCBlack,     IRCDefault,   2));
	assert(testColor(L"\00312a",    IRCLightBlue, IRCDefault,   3));
	assert(testColor(L"\00312,a",   IRCLightBlue, IRCDefault,   3));
	assert(testColor(L"\003123",    IRCLightBlue, IRCDefault,   3));
	assert(testColor(L"\0031,1a",   IRCBlack,     IRCBlack,     4));
	assert(testColor(L"\0031,12a",  IRCBlack,     IRCLightBlue, 5));
	assert(testColor(L"\0031,123",  IRCBlack,     IRCLightBlue, 5));
	assert(testColor(L"\00312,1a",  IRCLightBlue, IRCBlack,     5));
	assert(testColor(L"\00312,12a", IRCLightBlue, IRCLightBlue, 6));
	assert(testColor(L"\00312,123", IRCLightBlue, IRCLightBlue, 6));

	assert(testColor(L"\00316,16a", IRCDefault, IRCDefault, 6));
	assert(testColor(L"\00399,99a", IRCDefault, IRCDefault, 6));

	assert(testSplits(L""));
	assert(testSplits(L"ab"));
	assert(testSplits(L"\002"));
	assert(testSplits(L"\002ab"));
	assert(testSplits(L"a\002b"));
	assert(testSplits(L"\002\003"));
	assert(testSplits(L"a\002\003b"));
	assert(testSplits(L"a\0031b"));
	assert(testSplits(L"a\00312b"));
	assert(testSplits(L"a\00312,1b"));
	assert(testSplits(L"a\00312,12b"));
}

#endif
