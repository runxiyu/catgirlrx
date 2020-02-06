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

#include <stdio.h>
#include <stdlib.h>

#include "chat.h"

typedef void Command(size_t id, char *params);

static void commandQuote(size_t id, char *params) {
	(void)id;
	ircFormat("%s\r\n", params);
}

static void commandPrivmsg(size_t id, char *params) {
	ircFormat("PRIVMSG %s :%s\r\n", idNames[id], params);
	struct Message msg = {
		.nick = self.nick,
		.user = self.user,
		.cmd = "PRIVMSG",
		.params[0] = idNames[id],
		.params[1] = params,
	};
	handle(msg);
}

static void commandNotice(size_t id, char *params) {
	ircFormat("NOTICE %s :%s\r\n", idNames[id], params);
	struct Message msg = {
		.nick = self.nick,
		.user = self.user,
		.cmd = "NOTICE",
		.params[0] = idNames[id],
		.params[1] = params,
	};
	handle(msg);
}

static void commandMe(size_t id, char *params) {
	char buf[512];
	snprintf(buf, sizeof(buf), "\1ACTION %s\1", params);
	commandPrivmsg(id, buf);
}

static void commandQuit(size_t id, char *params) {
	(void)id;
	set(&self.quit, (params ? params : "Goodbye"));
}

static void commandWindow(size_t id, char *params) {
	(void)id;
	if (!params) return;
	uiShowNum(strtoul(params, NULL, 10));
}

static const struct Handler {
	const char *cmd;
	Command *fn;
} Commands[] = {
	{ "/me", commandMe },
	{ "/notice", commandNotice },
	{ "/quit", commandQuit },
	{ "/quote", commandQuote },
	{ "/window", commandWindow },
};

static int compar(const void *cmd, const void *_handler) {
	const struct Handler *handler = _handler;
	return strcmp(cmd, handler->cmd);
}

const char *commandIsPrivmsg(size_t id, const char *input) {
	if (id == Network || id == Debug) return NULL;
	if (input[0] != '/') return input;
	const char *space = strchr(&input[1], ' ');
	const char *slash = strchr(&input[1], '/');
	if (slash && (!space || slash < space)) return input;
	return NULL;
}

const char *commandIsNotice(size_t id, const char *input) {
	if (id == Network || id == Debug) return NULL;
	if (strncmp(input, "/notice ", 8)) return NULL;
	return &input[8];
}

const char *commandIsAction(size_t id, const char *input) {
	if (id == Network || id == Debug) return NULL;
	if (strncmp(input, "/me ", 4)) return NULL;
	return &input[4];
}

void command(size_t id, char *input) {
	if (id == Debug && input[0] != '/') {
		commandQuote(id, input);
	} else if (commandIsPrivmsg(id, input)) {
		commandPrivmsg(id, input);
	} else if (input[0] == '/' && isdigit(input[1])) {
		commandWindow(id, &input[1]);
	} else {
		char *cmd = strsep(&input, " ");
		const struct Handler *handler = bsearch(
			cmd, Commands, ARRAY_LEN(Commands), sizeof(*handler), compar
		);
		if (handler) {
			handler->fn(id, input);
		} else {
			uiFormat(id, Hot, NULL, "No such command %s", cmd);
		}
	}
}
