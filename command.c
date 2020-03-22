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

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "chat.h"

typedef void Command(uint id, char *params);

static void commandDebug(uint id, char *params) {
	(void)id;
	(void)params;
	self.debug ^= true;
	uiFormat(
		Debug, Warm, NULL,
		"\3%dDebug is %s", Gray, (self.debug ? "on" : "off")
	);
}

static void commandQuote(uint id, char *params) {
	(void)id;
	if (params) ircFormat("%s\r\n", params);
}

static void echoMessage(char *cmd, uint id, char *params) {
	if (!params) return;
	ircFormat("%s %s :%s\r\n", cmd, idNames[id], params);
	struct Message msg = {
		.nick = self.nick,
		.user = self.user,
		.cmd = cmd,
		.params[0] = idNames[id],
		.params[1] = params,
	};
	handle(msg);
}

static void splitMessage(char *cmd, uint id, char *params) {
	if (!params) return;
	// FIXME: Get USERLEN, HOSTLEN from ISUPPORT and assume worst case if
	// self.user and self.host are unset?
	const char *nick = self.nick;
	const char *user = (self.user ? self.user : "*");
	const char *host = (self.host ? self.host : "*");
	int overhead = snprintf(
		NULL, 0, ":%s!%s@%s %s %s :\r\n",
		nick, user, host, cmd, idNames[id]
	);
	assert(overhead > 0 && overhead < 512);
	int chunk = 512 - overhead;
	if (strlen(params) <= (size_t)chunk) {
		echoMessage(cmd, id, params);
		return;
	}

	while (*params) {
		int len = 0;
		for (int n = 0; len + n <= chunk; len += n) {
			n = mblen(&params[len], 1 + strlen(&params[len]));
			if (n < 0) {
				n = 1;
				mblen(NULL, 0);
			}
			if (!n) break;
		}
		char ch = params[len];
		params[len] = '\0';
		echoMessage(cmd, id, params);
		params[len] = ch;
		params += len;
	}
}

static void commandPrivmsg(uint id, char *params) {
	splitMessage("PRIVMSG", id, params);
}

static void commandNotice(uint id, char *params) {
	splitMessage("NOTICE", id, params);
}

static void commandMe(uint id, char *params) {
	char buf[512];
	snprintf(buf, sizeof(buf), "\1ACTION %s\1", (params ? params : ""));
	echoMessage("PRIVMSG", id, buf);
}

static void commandMsg(uint id, char *params) {
	id = idFor(strsep(&params, " "));
	splitMessage("PRIVMSG", id, params);
}

static void commandJoin(uint id, char *params) {
	if (!params) params = idNames[id];
	uint count = 1;
	for (char *ch = params; *ch && *ch != ' '; ++ch) {
		if (*ch == ',') count++;
	}
	ircFormat("JOIN %s\r\n", params);
	replies.join += count;
	replies.topic += count;
	replies.names += count;
}

static void commandPart(uint id, char *params) {
	if (params) {
		ircFormat("PART %s :%s\r\n", idNames[id], params);
	} else {
		ircFormat("PART %s\r\n", idNames[id]);
	}
}

static void commandQuit(uint id, char *params) {
	(void)id;
	set(&self.quit, (params ? params : "nyaa~"));
}

static void commandNick(uint id, char *params) {
	(void)id;
	if (!params) return;
	ircFormat("NICK :%s\r\n", params);
}

static void commandAway(uint id, char *params) {
	(void)id;
	if (params) {
		ircFormat("AWAY :%s\r\n", params);
	} else {
		ircFormat("AWAY\r\n");
	}
	replies.away++;
}

static void commandTopic(uint id, char *params) {
	if (params) {
		ircFormat("TOPIC %s :%s\r\n", idNames[id], params);
	} else {
		ircFormat("TOPIC %s\r\n", idNames[id]);
		replies.topic++;
	}
}

static void commandNames(uint id, char *params) {
	(void)params;
	ircFormat("NAMES %s\r\n", idNames[id]);
	replies.names++;
}

static void commandInvite(uint id, char *params) {
	if (!params) return;
	char *nick = strsep(&params, " ");
	ircFormat("INVITE %s %s\r\n", nick, idNames[id]);
}

static void commandKick(uint id, char *params) {
	if (!params) return;
	char *nick = strsep(&params, " ");
	if (params) {
		ircFormat("KICK %s %s :%s\r\n", idNames[id], nick, params);
	} else {
		ircFormat("KICK %s %s\r\n", idNames[id], nick);
	}
}

static void commandMode(uint id, char *params) {
	if (id == Network) {
		if (params) {
			ircFormat("MODE %s %s\r\n", self.nick, params);
		} else {
			ircFormat("MODE %s\r\n", self.nick);
		}
	} else {
		if (params) {
			ircFormat("MODE %s %s\r\n", idNames[id], params);
		} else {
			ircFormat("MODE %s\r\n", idNames[id]);
		}
	}
}

static void channelListMode(uint id, char pm, char l, char *params) {
	int count = 1;
	for (char *ch = params; *ch; ++ch) {
		if (*ch == ' ') count++;
	}
	char modes[ParamCap - 2] = { l, l, l, l, l, l, l, l, l, l, l, l, l };
	ircFormat("MODE %s %c%.*s %s\r\n", idNames[id], pm, count, modes, params);
}

static void commandBan(uint id, char *params) {
	if (params) {
		channelListMode(id, '+', 'b', params);
	} else {
		ircFormat("MODE %s b\r\n", idNames[id]);
		replies.ban++;
	}
}

static void commandUnban(uint id, char *params) {
	if (!params) return;
	channelListMode(id, '-', 'b', params);
}

static void commandExcept(uint id, char *params) {
	if (params) {
		channelListMode(id, '+', network.excepts, params);
	} else {
		ircFormat("MODE %s %c\r\n", idNames[id], network.excepts);
		replies.excepts++;
	}
}

static void commandUnexcept(uint id, char *params) {
	if (!params) return;
	channelListMode(id, '-', network.excepts, params);
}

static void commandInvex(uint id, char *params) {
	if (params) {
		channelListMode(id, '+', network.invex, params);
	} else {
		ircFormat("MODE %s %c\r\n", idNames[id], network.invex);
		replies.invex++;
	}
}

static void commandUninvex(uint id, char *params) {
	if (!params) return;
	channelListMode(id, '-', network.invex, params);
}

static void commandList(uint id, char *params) {
	(void)id;
	if (params) {
		ircFormat("LIST :%s\r\n", params);
	} else {
		ircFormat("LIST\r\n");
	}
	replies.list++;
}

static void commandWhois(uint id, char *params) {
	(void)id;
	if (!params) return;
	ircFormat("WHOIS :%s\r\n", params);
	replies.whois++;
}

static void commandNS(uint id, char *params) {
	(void)id;
	if (params) ircFormat("PRIVMSG NickServ :%s\r\n", params);
}

static void commandCS(uint id, char *params) {
	(void)id;
	if (params) ircFormat("PRIVMSG ChanServ :%s\r\n", params);
}

static void commandQuery(uint id, char *params) {
	if (!params) return;
	uint query = idFor(params);
	idColors[query] = completeColor(id, params);
	uiShowID(query);
}

static void commandWindow(uint id, char *params) {
	if (!params) return;
	if (isdigit(params[0])) {
		uiShowNum(strtoul(params, NULL, 10));
	} else {
		id = idFind(params);
		if (id) uiShowID(id);
	}
}

static void commandMove(uint id, char *params) {
	if (!params) return;
	char *name = strsep(&params, " ");
	if (params) {
		id = idFind(name);
		if (id) uiMoveID(id, strtoul(params, NULL, 10));
	} else {
		uiMoveID(id, strtoul(name, NULL, 10));
	}
}

static void commandClose(uint id, char *params) {
	if (!params) {
		uiCloseID(id);
	} else if (isdigit(params[0])) {
		uiCloseNum(strtoul(params, NULL, 10));
	} else {
		id = idFind(params);
		if (id) uiCloseID(id);
	}
}

static void commandOpen(uint id, char *params) {
	if (!params) {
		urlOpenCount(id, 1);
	} else if (isdigit(params[0])) {
		urlOpenCount(id, strtoul(params, NULL, 10));
	} else {
		urlOpenMatch(id, params);
	}
}

static void commandCopy(uint id, char *params) {
	urlCopyMatch(id, params);
}

static void commandExec(uint id, char *params) {
	execID = id;

	pid_t pid = fork();
	if (pid < 0) err(EX_OSERR, "fork");
	if (pid) return;

	const char *shell = getenv("SHELL");
	if (!shell) shell = "/bin/sh";

	close(STDIN_FILENO);
	dup2(execPipe[1], STDOUT_FILENO);
	dup2(utilPipe[1], STDERR_FILENO);
	execlp(shell, shell, "-c", params, NULL);
	warn("%s", shell);
	_exit(EX_UNAVAILABLE);
}

static void commandHelp(uint id, char *params) {
	(void)id;
	uiHide();

	pid_t pid = fork();
	if (pid < 0) err(EX_OSERR, "fork");
	if (pid) return;

	char buf[256];
	snprintf(buf, sizeof(buf), "ip%s$", (params ? params : "COMMANDS"));
	setenv("LESS", buf, 1);
	execlp("man", "man", "1", "catgirl", NULL);
	dup2(utilPipe[1], STDERR_FILENO);
	warn("man");
	_exit(EX_UNAVAILABLE);
}

static const struct Handler {
	const char *cmd;
	Command *fn;
	bool restricted;
} Commands[] = {
	{ "/away", .fn = commandAway },
	{ "/ban", .fn = commandBan },
	{ "/close", .fn = commandClose },
	{ "/copy", .fn = commandCopy, .restricted = true },
	{ "/cs", .fn = commandCS },
	{ "/debug", .fn = commandDebug, .restricted = true },
	{ "/except", .fn = commandExcept },
	{ "/exec", .fn = commandExec, .restricted = true },
	{ "/help", .fn = commandHelp },
	{ "/invex", .fn = commandInvex },
	{ "/invite", .fn = commandInvite },
	{ "/join", .fn = commandJoin, .restricted = true },
	{ "/kick", .fn = commandKick },
	{ "/list", .fn = commandList },
	{ "/me", .fn = commandMe },
	{ "/mode", .fn = commandMode },
	{ "/move", .fn = commandMove },
	{ "/msg", .fn = commandMsg, .restricted = true },
	{ "/names", .fn = commandNames },
	{ "/nick", .fn = commandNick },
	{ "/notice", .fn = commandNotice },
	{ "/ns", .fn = commandNS },
	{ "/open", .fn = commandOpen, .restricted = true },
	{ "/part", .fn = commandPart },
	{ "/query", .fn = commandQuery, .restricted = true },
	{ "/quit", .fn = commandQuit },
	{ "/quote", .fn = commandQuote, .restricted = true },
	{ "/say", .fn = commandPrivmsg },
	{ "/topic", .fn = commandTopic },
	{ "/unban", .fn = commandUnban },
	{ "/unexcept", .fn = commandUnexcept },
	{ "/uninvex", .fn = commandUninvex },
	{ "/whois", .fn = commandWhois },
	{ "/window", .fn = commandWindow },
};

static int compar(const void *cmd, const void *_handler) {
	const struct Handler *handler = _handler;
	return strcmp(cmd, handler->cmd);
}

const char *commandIsPrivmsg(uint id, const char *input) {
	if (id == Network || id == Debug) return NULL;
	if (input[0] != '/') return input;
	const char *space = strchr(&input[1], ' ');
	const char *slash = strchr(&input[1], '/');
	if (slash && (!space || slash < space)) return input;
	return NULL;
}

const char *commandIsNotice(uint id, const char *input) {
	if (id == Network || id == Debug) return NULL;
	if (strncmp(input, "/notice ", 8)) return NULL;
	return &input[8];
}

const char *commandIsAction(uint id, const char *input) {
	if (id == Network || id == Debug) return NULL;
	if (strncmp(input, "/me ", 4)) return NULL;
	return &input[4];
}

void command(uint id, char *input) {
	if (id == Debug && input[0] != '/' && !self.restricted) {
		commandQuote(id, input);
		return;
	} else if (!input[0]) {
		return;
	} else if (commandIsPrivmsg(id, input)) {
		commandPrivmsg(id, input);
		return;
	} else if (input[0] == '/' && isdigit(input[1])) {
		commandWindow(id, &input[1]);
		return;
	}

	const char *cmd = strsep(&input, " ");
	const char *unique = complete(None, cmd);
	if (unique && !complete(None, cmd)) {
		cmd = unique;
		completeReject();
	}

	const struct Handler *handler = bsearch(
		cmd, Commands, ARRAY_LEN(Commands), sizeof(*handler), compar
	);
	if (!handler) {
		uiFormat(id, Warm, NULL, "No such command %s", cmd);
		return;
	}
	if (self.restricted && handler->restricted) {
		uiFormat(id, Warm, NULL, "Command %s is restricted", cmd);
		return;
	}

	if (input) {
		input += strspn(input, " ");
		size_t len = strlen(input);
		while (input[len - 1] == ' ') input[--len] = '\0';
		if (!input[0]) input = NULL;
	}
	handler->fn(id, input);
}

void commandComplete(void) {
	for (size_t i = 0; i < ARRAY_LEN(Commands); ++i) {
		completeAdd(None, Commands[i].cmd, Default);
	}
}
