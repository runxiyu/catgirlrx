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
 *
 * Additional permission under GNU GPL version 3 section 7:
 *
 * If you modify this Program, or any covered work, by linking or
 * combining it with LibreSSL (or a modified version of that library),
 * containing parts covered by the terms of the OpenSSL License and the
 * original SSLeay license, the licensors of this Program grant you
 * additional permission to convey the resulting work. Corresponding
 * Source for a non-source form of such a combination shall include the
 * source code for the parts of LibreSSL used as well as that of the
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

struct Ignore ignore;

const char *ignoreAdd(const char *pattern) {
	if (ignore.len == IgnoreCap) errx(EX_CONFIG, "ignore limit exceeded");
	uint ex = 0, sp = 0;
	for (const char *ch = pattern; *ch; ++ch) {
		if (*ch == '!') ex++;
		if (*ch == ' ') sp++;
	}
	char **dest = &ignore.patterns[ignore.len++];
	if (!ex && !sp) {
		asprintf(dest, "%s!*@* * * *", pattern);
	} else if (sp < 1) {
		asprintf(dest, "%s * * *", pattern);
	} else if (sp < 2) {
		asprintf(dest, "%s * *", pattern);
	} else if (sp < 3) {
		asprintf(dest, "%s *", pattern);
	} else {
		*dest = strdup(pattern);
	}
	if (!*dest) err(EX_OSERR, "strdup");
	return *dest;
}

bool ignoreRemove(const char *pattern) {
	bool found = false;
	for (size_t i = 0; i < ignore.len; ++i) {
		if (strcasecmp(ignore.patterns[i], pattern)) continue;
		free(ignore.patterns[i]);
		ignore.patterns[i] = ignore.patterns[--ignore.len];
		found = true;
	}
	return found;
}

enum Heat ignoreCheck(enum Heat heat, uint id, const struct Message *msg) {
	char match[512];
	snprintf(
		match, sizeof(match), "%s!%s@%s %s %s %s",
		msg->nick, msg->user, msg->host,
		msg->cmd, idNames[id], (msg->params[1] ?: "")
	);
	for (size_t i = 0; i < ignore.len; ++i) {
		if (fnmatch(ignore.patterns[i], match, FNM_CASEFOLD)) continue;
		return Ice;
	}
	return heat;
}
