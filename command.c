/* Copyright (C) 2020  June McEnroe <june@causal.agency>
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
 *
 * Additional permission under GNU GPL version 3 section 7:
 *
 * If you modify this Program, or any covered work, by linking or
 * combining it with OpenSSL (or a modified version of that library),
 * containing parts covered by the terms of the OpenSSL License and the
 * original SSLeay license, the licensors of this Program grant you
 * additional permission to convey the resulting work. Corresponding
 * Source for a non-source form of such a combination shall include the
 * source code for the parts of OpenSSL used as well as that of the
 * covered work.
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
	handle(&msg);
}

static int splitChunk(const char *cmd, uint id) {
	int overhead = snprintf(
		NULL, 0, ":%s!%*s@%*s %s %s :\r\n",
		self.nick,
		(self.user ? 0 : network.userLen), (self.user ?: "*"),
		(self.host ? 0 : network.hostLen), (self.host ?: "*"),
		cmd, idNames[id]
	);
	assert(overhead > 0 && overhead < 512);
	return 512 - overhead;
}

static int splitLen(int chunk, const char *params) {
	int len = 0;
	size_t cap = 1 + strlen(params);
	for (int n = 0; params[len] != '\n' && len + n <= chunk; len += n) {
		n = mblen(&params[len], cap - len);
		if (n < 0) {
			n = 1;
			mblen(NULL, 0);
		}
		if (!n) break;
	}
	return len;
}

static void splitMessage(char *cmd, uint id, char *params) {
	if (!params) return;
	int chunk = splitChunk(cmd, id);
	if (strlen(params) <= (size_t)chunk && !strchr(params, '\n')) {
		echoMessage(cmd, id, params);
		return;
	}
	while (*params) {
		int len = splitLen(chunk, params);
		char ch = params[len];
		params[len] = '\0';
		echoMessage(cmd, id, params);
		params[len] = ch;
		params += len;
		if (ch == '\n') params++;
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
	if (!params) params = "";
	int chunk = splitChunk("PRIVMSG \1ACTION\1", id);
	if (strlen(params) <= (size_t)chunk && !strchr(params, '\n')) {
		snprintf(buf, sizeof(buf), "\1ACTION %s\1", params);
		echoMessage("PRIVMSG", id, buf);
		return;
	}
	while (*params) {
		int len = splitLen(chunk, params);
		snprintf(buf, sizeof(buf), "\1ACTION %.*s\1", len, params);
		echoMessage("PRIVMSG", id, buf);
		params += len;
		if (*params == '\n') params++;
	}
}

static void commandMsg(uint id, char *params) {
	if (!params) return;
	char *nick = strsep(&params, " ");
	uint msg = idFor(nick);
	if (idColors[msg] == Default) {
		idColors[msg] = completeColor(id, nick);
	}
	if (params) {
		splitMessage("PRIVMSG", msg, params);
	} else {
		windowShow(windowFor(msg));
	}
}

static void commandJoin(uint id, char *params) {
	if (!params && id == Network) params = self.invited;
	if (!params) params = idNames[id];
	uint count = 1;
	for (char *ch = params; *ch && *ch != ' '; ++ch) {
		if (*ch == ',') count++;
	}
	ircFormat("JOIN %s\r\n", params);
	replies[ReplyJoin] += count;
	replies[ReplyTopic] += count;
	replies[ReplyNames] += count;
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
	set(&self.quit, (params ?: "nyaa~"));
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
	replies[ReplyAway]++;
}

static void commandSetname(uint id, char *params) {
	(void)id;
	if (!params) return;
	ircFormat("SETNAME :%s\r\n", params);
}

static void commandTopic(uint id, char *params) {
	if (params) {
		ircFormat("TOPIC %s :%s\r\n", idNames[id], params);
	} else {
		ircFormat("TOPIC %s\r\n", idNames[id]);
		replies[ReplyTopic]++;
	}
}

static void commandNames(uint id, char *params) {
	(void)params;
	ircFormat("NAMES %s\r\n", idNames[id]);
	replies[ReplyNames]++;
}

static void commandOps(uint id, char *params) {
	(void)params;
	ircFormat("WHO %s\r\n", idNames[id]);
	replies[ReplyWho]++;
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
			replies[ReplyMode]++;
		}
	} else {
		if (params) {
			if (!params[1] || (params[0] == '+' && !params[2])) {
				char m = (params[0] == '+' ? params[1] : params[0]);
				if (m == 'b') replies[ReplyBan]++;
				if (m == network.excepts) replies[ReplyExcepts]++;
				if (m == network.invex) replies[ReplyInvex]++;
			}
			ircFormat("MODE %s %s\r\n", idNames[id], params);
		} else {
			ircFormat("MODE %s\r\n", idNames[id]);
			replies[ReplyMode]++;
		}
	}
}

static void channelListMode(uint id, char pm, char l, const char *params) {
	int count = 1;
	for (const char *ch = params; *ch; ++ch) {
		if (*ch == ' ') count++;
	}
	char modes[13 + 1] = { l, l, l, l, l, l, l, l, l, l, l, l, l, '\0' };
	ircFormat("MODE %s %c%.*s %s\r\n", idNames[id], pm, count, modes, params);
}

static void commandOp(uint id, char *params) {
	if (params) {
		channelListMode(id, '+', 'o', params);
	} else {
		ircFormat("CS OP %s\r\n", idNames[id]);
	}
}

static void commandDeop(uint id, char *params) {
	channelListMode(id, '-', 'o', (params ?: self.nick));
}

static void commandVoice(uint id, char *params) {
	if (params) {
		channelListMode(id, '+', 'v', params);
	} else {
		ircFormat("CS VOICE %s\r\n", idNames[id]);
	}
}

static void commandDevoice(uint id, char *params) {
	channelListMode(id, '-', 'v', (params ?: self.nick));
}

static void commandBan(uint id, char *params) {
	if (params) {
		channelListMode(id, '+', 'b', params);
	} else {
		ircFormat("MODE %s b\r\n", idNames[id]);
		replies[ReplyBan]++;
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
		replies[ReplyExcepts]++;
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
		replies[ReplyInvex]++;
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
	replies[ReplyList]++;
}

static void commandWhois(uint id, char *params) {
	(void)id;
	if (!params) params = self.nick;
	uint count = 1;
	for (char *ch = params; *ch; ++ch) {
		if (*ch == ',') count++;
	}
	ircFormat("WHOIS %s\r\n", params);
	replies[ReplyWhois] += count;
}

static void commandWhowas(uint id, char *params) {
	(void)id;
	if (!params) return;
	ircFormat("WHOWAS %s\r\n", params);
	replies[ReplyWhowas]++;
}

static void commandNS(uint id, char *params) {
	(void)id;
	ircFormat("NS %s\r\n", (params ?: "HELP"));
}

static void commandCS(uint id, char *params) {
	(void)id;
	ircFormat("CS %s\r\n", (params ?: "HELP"));
}

static void commandQuery(uint id, char *params) {
	if (!params) return;
	uint query = idFor(params);
	if (idColors[query] == Default) {
		idColors[query] = completeColor(id, params);
	}
	windowShow(windowFor(query));
}

static void commandWindow(uint id, char *params) {
	if (!params) {
		windowList();
	} else if (isdigit(params[0])) {
		windowShow(strtoul(params, NULL, 10));
	} else {
		id = idFind(params);
		if (id) {
			windowShow(windowFor(id));
			return;
		}
		for (const char *match; (match = completeSubstr(None, params));) {
			id = idFind(match);
			if (!id) continue;
			completeAccept();
			windowShow(windowFor(id));
			break;
		}
	}
}

static void commandMove(uint id, char *params) {
	if (!params) return;
	char *name = strsep(&params, " ");
	if (params) {
		id = idFind(name);
		if (id) windowMove(windowFor(id), strtoul(params, NULL, 10));
	} else {
		windowMove(windowFor(id), strtoul(name, NULL, 10));
	}
}

static void commandClose(uint id, char *params) {
	if (!params) {
		windowClose(windowFor(id));
	} else if (isdigit(params[0])) {
		windowClose(strtoul(params, NULL, 10));
	} else {
		id = idFind(params);
		if (id) windowClose(windowFor(id));
	}
}

static void commandOpen(uint id, char *params) {
	if (!params) {
		urlOpenCount(id, 1);
	} else if (isdigit(params[0]) && !params[1]) {
		urlOpenCount(id, params[0] - '0');
	} else {
		urlOpenMatch(id, params);
	}
}

static void commandCopy(uint id, char *params) {
	urlCopyMatch(id, params);
}

static void commandFilter(enum Heat heat, uint id, char *params) {
	if (params) {
		struct Filter filter = filterAdd(heat, params);
		uiFormat(
			id, Cold, NULL, "%sing \3%02d%s %s %s %s",
			(heat == Hot ? "Highlight" : "Ignor"), Brown, filter.mask,
			(filter.cmd ?: ""), (filter.chan ?: ""), (filter.mesg ?: "")
		);
	} else {
		for (size_t i = 0; i < FilterCap && filters[i].mask; ++i) {
			if (filters[i].heat != heat) continue;
			uiFormat(
				Network, Warm, NULL, "%sing \3%02d%s %s %s %s",
				(heat == Hot ? "Highlight" : "Ignor"), Brown, filters[i].mask,
				(filters[i].cmd ?: ""), (filters[i].chan ?: ""),
				(filters[i].mesg ?: "")
			);
		}
	}
}

static void commandUnfilter(enum Heat heat, uint id, char *params) {
	if (!params) return;
	struct Filter filter = filterParse(heat, params);
	bool found = filterRemove(filter);
	uiFormat(
		id, Cold, NULL, "%s %sing \3%02d%s %s %s %s",
		(found ? "No longer" : "Not"), (heat == Hot ? "highlight" : "ignor"),
		Brown, filter.mask, (filter.cmd ?: ""), (filter.chan ?: ""),
		(filter.mesg ?: "")
	);
}

static void commandHighlight(uint id, char *params) {
	commandFilter(Hot, id, params);
}
static void commandIgnore(uint id, char *params) {
	commandFilter(Ice, id, params);
}
static void commandUnhighlight(uint id, char *params) {
	commandUnfilter(Hot, id, params);
}
static void commandUnignore(uint id, char *params) {
	commandUnfilter(Ice, id, params);
}

static void commandExec(uint id, char *params) {
	execID = id;

	pid_t pid = fork();
	if (pid < 0) err(EX_OSERR, "fork");
	if (pid) return;

	setsid();
	close(STDIN_FILENO);
	dup2(execPipe[1], STDOUT_FILENO);
	dup2(utilPipe[1], STDERR_FILENO);

	const char *shell = getenv("SHELL") ?: "/bin/sh";
	execl(shell, shell, "-c", params, NULL);
	warn("%s", shell);
	_exit(EX_UNAVAILABLE);
}

static void commandHelp(uint id, char *params) {
	(void)id;

	if (params) {
		ircFormat("HELP :%s\r\n", params);
		replies[ReplyHelp]++;
		return;
	}
	if (self.restricted) {
		uiFormat(id, Warm, NULL, "See catgirl(1) or /help index");
		return;
	}

	uiHide();
	pid_t pid = fork();
	if (pid < 0) err(EX_OSERR, "fork");
	if (pid) return;

	char buf[256];
	snprintf(buf, sizeof(buf), "%sp^COMMANDS$", (getenv("LESS") ?: ""));
	setenv("LESS", buf, 1);
	execlp("man", "man", "1", "catgirl", NULL);
	dup2(utilPipe[1], STDERR_FILENO);
	warn("man");
	_exit(EX_UNAVAILABLE);
}

enum Flag {
	BIT(Multiline),
	BIT(Restrict),
	BIT(Kiosk),
};

static const struct Handler {
	const char *cmd;
	Command *fn;
	enum Flag flags;
	enum Cap caps;
} Commands[] = {
	{ "/away", commandAway, 0, 0 },
	{ "/ban", commandBan, 0, 0 },
	{ "/close", commandClose, 0, 0 },
	{ "/copy", commandCopy, Restrict | Kiosk, 0 },
	{ "/cs", commandCS, 0, 0 },
	{ "/debug", commandDebug, Kiosk, 0 },
	{ "/deop", commandDeop, 0, 0 },
	{ "/devoice", commandDevoice, 0, 0 },
	{ "/except", commandExcept, 0, 0 },
	{ "/exec", commandExec, Multiline | Restrict | Kiosk, 0 },
	{ "/help", commandHelp, 0, 0 }, // Restrict special case.
	{ "/highlight", commandHighlight, 0, 0 },
	{ "/ignore", commandIgnore, 0, 0 },
	{ "/invex", commandInvex, 0, 0 },
	{ "/invite", commandInvite, 0, 0 },
	{ "/join", commandJoin, Kiosk, 0 },
	{ "/kick", commandKick, 0, 0 },
	{ "/list", commandList, Kiosk, 0 },
	{ "/me", commandMe, Multiline, 0 },
	{ "/mode", commandMode, 0, 0 },
	{ "/move", commandMove, 0, 0 },
	{ "/msg", commandMsg, Multiline | Kiosk, 0 },
	{ "/names", commandNames, 0, 0 },
	{ "/nick", commandNick, 0, 0 },
	{ "/notice", commandNotice, Multiline, 0 },
	{ "/ns", commandNS, 0, 0 },
	{ "/o", commandOpen, Restrict | Kiosk, 0 },
	{ "/op", commandOp, 0, 0 },
	{ "/open", commandOpen, Restrict | Kiosk, 0 },
	{ "/ops", commandOps, 0, 0 },
	{ "/part", commandPart, Kiosk, 0 },
	{ "/query", commandQuery, Kiosk, 0 },
	{ "/quit", commandQuit, 0, 0 },
	{ "/quote", commandQuote, Multiline | Kiosk, 0 },
	{ "/say", commandPrivmsg, Multiline, 0 },
	{ "/setname", commandSetname, 0, CapSetname },
	{ "/topic", commandTopic, 0, 0 },
	{ "/unban", commandUnban, 0, 0 },
	{ "/unexcept", commandUnexcept, 0, 0 },
	{ "/unhighlight", commandUnhighlight, 0, 0 },
	{ "/unignore", commandUnignore, 0, 0 },
	{ "/uninvex", commandUninvex, 0, 0 },
	{ "/voice", commandVoice, 0, 0 },
	{ "/whois", commandWhois, 0, 0 },
	{ "/whowas", commandWhowas, 0, 0 },
	{ "/window", commandWindow, 0, 0 },
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

size_t commandWillSplit(uint id, const char *input) {
	int chunk;
	const char *params;
	if (NULL != (params = commandIsPrivmsg(id, input))) {
		chunk = splitChunk("PRIVMSG", id);
	} else if (NULL != (params = commandIsNotice(id, input))) {
		chunk = splitChunk("NOTICE", id);
	} else if (NULL != (params = commandIsAction(id, input))) {
		chunk = splitChunk("PRIVMSG \1ACTION\1", id);
	} else if (id != Network && id != Debug && !strncmp(input, "/say ", 5)) {
		params = &input[5];
		chunk = splitChunk("PRIVMSG", id);
	} else {
		return 0;
	}
	if (strlen(params) <= (size_t)chunk) return 0;
	for (
		int split;
		params[(split = splitLen(chunk, params))];
		params = &params[split + 1]
	) {
		if (params[split] == '\n') continue;
		return (params - input) + split;
	}
	return 0;
}

static bool commandAvailable(const struct Handler *handler) {
	if (handler->flags & Restrict && self.restricted) return false;
	if (handler->flags & Kiosk && self.kiosk) return false;
	if (handler->caps && (handler->caps & self.caps) != handler->caps) {
		return false;
	}
	return true;
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
	if (!commandAvailable(handler)) {
		uiFormat(id, Warm, NULL, "Command %s is unavailable", cmd);
		return;
	}

	if (input) {
		if (!(handler->flags & Multiline)) {
			input[strcspn(input, "\n")] = '\0';
		}
		input += strspn(input, " ");
		size_t len = strlen(input);
		while (input[len - 1] == ' ') input[--len] = '\0';
		if (!input[0]) input = NULL;
	}
	handler->fn(id, input);
}

void commandCompleteAdd(void) {
	for (size_t i = 0; i < ARRAY_LEN(Commands); ++i) {
		if (!commandAvailable(&Commands[i])) continue;
		completeAdd(None, Commands[i].cmd, Default);
	}
}
