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

#include <err.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sysexits.h>
#include <wchar.h>

#include "chat.h"

static wchar_t *wcssep(wchar_t **stringp, const wchar_t *delim) {
	wchar_t *orig = *stringp;
	if (!orig) return NULL;
	size_t i = wcscspn(orig, delim);
	*stringp = NULL;
	if (orig[i]) {
		orig[i] = '\0';
		*stringp = &orig[i + 1];
	}
	return orig;
}

static void privmsg(bool action, const wchar_t *mesg) {
	char *line;
	int send;
	asprintf(
		&line, ":%s!%s %nPRIVMSG %s :%s%ls%s",
		chat.nick, chat.user, &send, chat.chan,
		(action ? "\1ACTION " : ""), mesg, (action ? "\1" : "")
	);
	if (!line) err(EX_OSERR, "asprintf");
	ircFmt("%s\r\n", &line[send]);
	handle(line);
	free(line);
}

typedef void (*Handler)(wchar_t *params);

static void inputMe(wchar_t *params) {
	privmsg(true, params ? params : L"");
}

static void inputNick(wchar_t *params) {
	wchar_t *nick = wcssep(&params, L" ");
	if (nick) {
		ircFmt("NICK %ls\r\n", nick);
	} else {
		uiChat("/nick requires a name");
	}
}

static void inputWho(wchar_t *params) {
	(void)params;
	ircFmt("WHO %s\r\n", chat.chan);
}

static void inputQuit(wchar_t *params) {
	if (params) {
		ircFmt("QUIT :%ls\r\n", params);
	} else {
		ircFmt("QUIT :Goodbye\r\n");
	}
}

static const struct {
	const wchar_t *command;
	Handler handler;
} COMMANDS[] = {
	{ L"me", inputMe },
	{ L"names", inputWho },
	{ L"nick", inputNick },
	{ L"quit", inputQuit },
	{ L"who", inputWho },
};
static const size_t COMMANDS_LEN = sizeof(COMMANDS) / sizeof(COMMANDS[0]);

void input(wchar_t *input) {
	if (input[0] != '/') {
		privmsg(false, input);
		return;
	}
	input++;
	wchar_t *command = wcssep(&input, L" ");
	for (size_t i = 0; i < COMMANDS_LEN; ++i) {
		if (wcscmp(command, COMMANDS[i].command)) continue;
		COMMANDS[i].handler(input);
		return;
	}
	uiFmt("/%ls isn't a recognized command", command);
}
