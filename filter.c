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

#include <err.h>
#include <fnmatch.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

#include "chat.h"

struct Filter filters[FilterCap];
static size_t len;

struct Filter filterParse(enum Heat heat, char *pattern) {
	struct Filter filter = { .heat = heat };
	filter.mask = strsep(&pattern, " ");
	filter.cmd  = strsep(&pattern, " ");
	filter.chan = strsep(&pattern, " ");
	filter.mesg = pattern;
	return filter;
}

struct Filter filterAdd(enum Heat heat, const char *pattern) {
	if (len == FilterCap) errx(EX_CONFIG, "filter limit exceeded");
	char *own;
	if (!strchr(pattern, '!') && !strchr(pattern, ' ')) {
		int n = asprintf(&own, "%s!*@*", pattern);
		if (n < 0) err(EX_OSERR, "asprintf");
	} else {
		own = strdup(pattern);
		if (!own) err(EX_OSERR, "strdup");
	}
	struct Filter filter = filterParse(heat, own);
	filters[len++] = filter;
	return filter;
}

bool filterRemove(struct Filter filter) {
	bool found = false;
	for (size_t i = len - 1; i < len; --i) {
		if (filters[i].heat != filter.heat) continue;
		if (!filters[i].cmd != !filter.cmd) continue;
		if (!filters[i].chan != !filter.chan) continue;
		if (!filters[i].mesg != !filter.mesg) continue;
		if (strcasecmp(filters[i].mask, filter.mask)) continue;
		if (filter.cmd && strcasecmp(filters[i].cmd, filter.cmd)) continue;
		if (filter.chan && strcasecmp(filters[i].chan, filter.chan)) continue;
		if (filter.mesg && strcasecmp(filters[i].mesg, filter.mesg)) continue;
		free(filters[i].mask);
		memmove(&filters[i], &filters[i + 1], sizeof(*filters) * --len);
		filters[len] = (struct Filter) {0};
		found = true;
	}
	return found;
}

static bool filterTest(
	struct Filter filter, const char *mask, uint id, const struct Message *msg
) {
	if (fnmatch(filter.mask, mask, FNM_CASEFOLD)) return false;
	if (!filter.cmd) return true;
	if (fnmatch(filter.cmd, msg->cmd, FNM_CASEFOLD)) return false;
	if (!filter.chan) return true;
	if (fnmatch(filter.chan, idNames[id], FNM_CASEFOLD)) return false;
	if (!filter.mesg) return true;
	if (!msg->params[1]) return false;
	return !fnmatch(filter.mesg, msg->params[1], FNM_CASEFOLD);
}

enum { IcedCap = 8 };
static struct {
	size_t len;
	char *msgIDs[IcedCap];
} iced;

static void icedPush(const char *msgID) {
	if (!msgID) return;
	size_t i = iced.len % IcedCap;
	free(iced.msgIDs[i]);
	iced.msgIDs[i] = strdup(msgID);
	if (!iced.msgIDs[i]) err(EX_OSERR, "strdup");
	iced.len++;
}

enum Heat filterCheck(enum Heat heat, uint id, const struct Message *msg) {
	if (!len) return heat;

	if (msg->tags[TagReply]) {
		for (size_t i = 0; i < IcedCap; ++i) {
			if (!iced.msgIDs[i]) continue;
			if (strcmp(msg->tags[TagReply], iced.msgIDs[i])) continue;
			icedPush(msg->tags[TagMsgID]);
			return Ice;
		}
	}

	char mask[512];
	snprintf(mask, sizeof(mask), "%s!%s@%s", msg->nick, msg->user, msg->host);
	for (size_t i = 0; i < len; ++i) {
		if (!filterTest(filters[i], mask, id, msg)) continue;
		if (filters[i].heat == Ice) icedPush(msg->tags[TagMsgID]);
		return filters[i].heat;
	}
	return heat;
}
