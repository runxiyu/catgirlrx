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

static void privmsg(struct Tag tag, bool action, const char *mesg) {
	char *line;
	int send;
	asprintf(
		&line, ":%s!%s %nPRIVMSG %s :%s%s%s",
		self.nick, self.user, &send, tag.name,
		(action ? "\1ACTION " : ""), mesg, (action ? "\1" : "")
	);
	if (!line) err(EX_OSERR, "asprintf");
	ircFmt("%s\r\n", &line[send]);
	handle(line);
	free(line);
}

typedef void (*Handler)(struct Tag tag, char *params);

static void inputMe(struct Tag tag, char *params) {
	privmsg(tag, true, params ? params : "");
}

static void inputNick(struct Tag tag, char *params) {
	(void)tag;
	char *nick = strsep(&params, " ");
	if (nick) {
		ircFmt("NICK %s\r\n", nick);
	} else {
		uiLog(TAG_STATUS, L"/nick requires a name");
	}
}

static void inputJoin(struct Tag tag, char *params) {
	(void)tag;
	char *chan = strsep(&params, " ");
	if (chan) {
		ircFmt("JOIN %s\r\n", chan);
	} else {
		uiLog(TAG_STATUS, L"/join requires a channel");
	}
}

static void inputWho(struct Tag tag, char *params) {
	(void)params; // TODO
	ircFmt("WHO %s\r\n", tag.name);
}

static void inputTopic(struct Tag tag, char *params) {
	if (params) { // TODO
		ircFmt("TOPIC %s :%s\r\n", tag.name, params);
	} else {
		ircFmt("TOPIC %s\r\n", tag.name);
	}
}

static void inputQuit(struct Tag tag, char *params) {
	(void)tag;
	if (params) {
		ircFmt("QUIT :%s\r\n", params);
	} else {
		ircFmt("QUIT :Goodbye\r\n");
	}
}

static void inputUrl(struct Tag tag, char *params) {
	(void)params;
	urlList(tag);
}
static void inputOpen(struct Tag tag, char *params) {
	size_t at = (params ? strtoul(strsep(&params, "-,"), NULL, 0) : 1);
	size_t to = (params ? strtoul(params, NULL, 0) : at);
	urlOpen(tag, at - 1, to);
}

static void inputView(struct Tag tag, char *params) {
	char *view = strsep(&params, " ");
	if (!view) return;
	int num = strtol(view, &view, 0);
	if (view[0]) {
		uiViewTag(tagFor(view));
	} else {
		uiViewNum(num);
	}
}

static const struct {
	const char *command;
	Handler handler;
} COMMANDS[] = {
	{ "/join", inputJoin },
	{ "/me", inputMe },
	{ "/names", inputWho },
	{ "/nick", inputNick },
	{ "/open", inputOpen },
	{ "/quit", inputQuit },
	{ "/topic", inputTopic },
	{ "/url", inputUrl },
	{ "/view", inputView },
	{ "/who", inputWho },
};
static const size_t COMMANDS_LEN = sizeof(COMMANDS) / sizeof(COMMANDS[0]);

void input(struct Tag tag, char *input) {
	if (input[0] != '/') {
		if (tag.id == TAG_VERBOSE.id) {
			ircFmt("%s\r\n", input);
		} else if (tag.id != TAG_STATUS.id) {
			privmsg(tag, false, input);
		}
		return;
	}
	char *command = strsep(&input, " ");
	if (input && !input[0]) input = NULL;
	for (size_t i = 0; i < COMMANDS_LEN; ++i) {
		if (strcasecmp(command, COMMANDS[i].command)) continue;
		COMMANDS[i].handler(tag, input);
		return;
	}
	uiFmt(TAG_STATUS, "%s isn't a recognized command", command);
}

void inputTab(void) {
	for (size_t i = 0; i < COMMANDS_LEN; ++i) {
		tabTouch(TAG_NONE, COMMANDS[i].command);
	}
}
