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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chat.h"

typedef void Command(size_t id, char *params);

static void commandDebug(size_t id, char *params) {
	(void)id;
	(void)params;
	self.debug ^= true;
	uiFormat(
		Debug, Warm, NULL,
		"\3%dDebug is %s", Gray, (self.debug ? "on" : "off")
	);
}

static void commandQuote(size_t id, char *params) {
	(void)id;
	if (params) ircFormat("%s\r\n", params);
}

static void commandPrivmsg(size_t id, char *params) {
	if (!params || !params[0]) return;
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
	if (!params || !params[0]) return;
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
	snprintf(buf, sizeof(buf), "\1ACTION %s\1", (params ? params : ""));
	commandPrivmsg(id, buf);
}

static void commandJoin(size_t id, char *params) {
	size_t count = 1;
	if (params) {
		for (char *ch = params; *ch && *ch != ' '; ++ch) {
			if (*ch == ',') count++;
		}
	}
	ircFormat("JOIN %s\r\n", (params ? params : idNames[id]));
	replies.topic += count;
	replies.names += count;
}

static void commandPart(size_t id, char *params) {
	if (params) {
		ircFormat("PART %s :%s\r\n", idNames[id], params);
	} else {
		ircFormat("PART %s\r\n", idNames[id]);
	}
}

static void commandQuit(size_t id, char *params) {
	(void)id;
	set(&self.quit, (params ? params : "Goodbye"));
}

static void commandNick(size_t id, char *params) {
	(void)id;
	if (!params) return;
	ircFormat("NICK :%s\r\n", params);
}

static void commandTopic(size_t id, char *params) {
	if (params) {
		ircFormat("TOPIC %s :%s\r\n", idNames[id], params);
	} else {
		ircFormat("TOPIC %s\r\n", idNames[id]);
		replies.topic++;
	}
}

static void commandNames(size_t id, char *params) {
	(void)params;
	ircFormat("NAMES :%s\r\n", idNames[id]);
	replies.names++;
}

static void commandQuery(size_t id, char *params) {
	if (!params) return;
	size_t query = idFor(params);
	idColors[query] = completeColor(id, params);
	uiShowID(query);
}

static void commandWindow(size_t id, char *params) {
	if (!params) return;
	if (isdigit(params[0])) {
		uiShowNum(strtoul(params, NULL, 10));
	} else {
		id = idFind(params);
		if (id) uiShowID(id);
	}
}

static void commandClose(size_t id, char *params) {
	if (!params) {
		uiCloseID(id);
	} else if (isdigit(params[0])) {
		uiCloseNum(strtoul(params, NULL, 10));
	} else {
		id = idFind(params);
		if (id) uiCloseID(id);
	}
}

static void commandOpen(size_t id, char *params) {
	if (!params) {
		urlOpenCount(id, 1);
	} else if (isdigit(params[0])) {
		urlOpenCount(id, strtoul(params, NULL, 10));
	} else {
		urlOpenMatch(id, params);
	}
}

static const struct Handler {
	const char *cmd;
	Command *fn;
} Commands[] = {
	{ "/close", commandClose },
	{ "/debug", commandDebug },
	{ "/join", commandJoin },
	{ "/me", commandMe },
	{ "/names", commandNames },
	{ "/nick", commandNick },
	{ "/notice", commandNotice },
	{ "/open", commandOpen },
	{ "/part", commandPart },
	{ "/query", commandQuery },
	{ "/quit", commandQuit },
	{ "/quote", commandQuote },
	{ "/topic", commandTopic },
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
		const char *cmd = strsep(&input, " ");
		const char *unique = complete(None, cmd);
		if (unique && !complete(None, cmd)) {
			cmd = unique;
			completeReject();
		}
		const struct Handler *handler = bsearch(
			cmd, Commands, ARRAY_LEN(Commands), sizeof(*handler), compar
		);
		if (handler) {
			if (input && !input[0]) input = NULL;
			handler->fn(id, input);
		} else {
			uiFormat(id, Hot, NULL, "No such command %s", cmd);
		}
	}
}

void commandComplete(void) {
	for (size_t i = 0; i < ARRAY_LEN(Commands); ++i) {
		completeAdd(None, Commands[i].cmd, Default);
	}
}
