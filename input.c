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
#include <string.h>
#include <sysexits.h>

#include "chat.h"

static void privmsg(bool action, const char *mesg) {
	char *line;
	int send;
	asprintf(
		&line, ":%s!%s %nPRIVMSG %s :%s%s%s",
		chat.nick, chat.user, &send, chat.chan,
		(action ? "\1ACTION " : ""), mesg, (action ? "\1" : "")
	);
	if (!line) err(EX_OSERR, "asprintf");
	ircFmt("%s\r\n", &line[send]);
	handle(line);
	free(line);
}

typedef void (*Handler)(char *params);

static void inputMe(char *params) {
	privmsg(true, params ? params : "");
}

static void inputNick(char *params) {
	char *nick = strsep(&params, " ");
	if (nick) {
		ircFmt("NICK %s\r\n", nick);
	} else {
		uiLog(L"/nick requires a name");
	}
}

static void inputWho(char *params) {
	(void)params;
	ircFmt("WHO %s\r\n", chat.chan);
}

static void inputTopic(char *params) {
	if (params) {
		ircFmt("TOPIC %s :%s\r\n", chat.chan, params);
	} else {
		ircFmt("TOPIC %s\r\n", chat.chan);
	}
}

static void inputQuit(char *params) {
	if (params) {
		ircFmt("QUIT :%s\r\n", params);
	} else {
		ircFmt("QUIT :Goodbye\r\n");
	}
}

static const struct {
	const char *command;
	Handler handler;
} COMMANDS[] = {
	{ "me", inputMe },
	{ "names", inputWho },
	{ "nick", inputNick },
	{ "quit", inputQuit },
	{ "topic", inputTopic },
	{ "who", inputWho },
};
static const size_t COMMANDS_LEN = sizeof(COMMANDS) / sizeof(COMMANDS[0]);

void input(char *input) {
	if (input[0] != '/') {
		privmsg(false, input);
		return;
	}
	input++;
	char *command = strsep(&input, " ");
	for (size_t i = 0; i < COMMANDS_LEN; ++i) {
		if (strcmp(command, COMMANDS[i].command)) continue;
		COMMANDS[i].handler(input);
		return;
	}
	uiFmt("/%s isn't a recognized command", command);
}
