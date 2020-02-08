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

#include <err.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

#include "chat.h"

static const char *Pattern = {
	"("
	"cvs|"
	"ftp|"
	"git|"
	"gopher|"
	"http|"
	"https|"
	"irc|"
	"ircs|"
	"magnet|"
	"sftp|"
	"ssh|"
	"svn|"
	"telnet|"
	"vnc"
	")"
	":[^[:space:]>\"]+"
};
static regex_t Regex;

static void compile(void) {
	static bool compiled;
	if (compiled) return;
	compiled = true;
	int error = regcomp(&Regex, Pattern, REG_EXTENDED);
	if (!error) return;
	char buf[256];
	regerror(error, &Regex, buf, sizeof(buf));
	errx(EX_SOFTWARE, "regcomp: %s: %s", buf, Pattern);
}

enum { Cap = 32 };
static struct {
	size_t ids[Cap];
	char *nicks[Cap];
	char *urls[Cap];
	size_t len;
} ring;

static void push(size_t id, const char *nick, const char *url, size_t len) {
	size_t i = ring.len++ % Cap;
	free(ring.nicks[i]);
	free(ring.urls[i]);
	ring.ids[i] = id;
	ring.nicks[i] = NULL;
	if (nick) {
		ring.nicks[i] = strdup(nick);
		if (!ring.nicks[i]) err(EX_OSERR, "strdup");
	}
	ring.urls[i] = strndup(url, len);
	if (!ring.urls[i]) err(EX_OSERR, "strndup");
}

void urlScan(size_t id, const char *nick, const char *mesg) {
	if (!mesg) return;
	compile();
	regmatch_t match = {0};
	for (const char *ptr = mesg; *ptr; ptr += match.rm_eo) {
		if (regexec(&Regex, ptr, 1, &match, 0)) break;
		push(id, nick, &ptr[match.rm_so], match.rm_eo - match.rm_so);
	}
}

void urlOpenCount(size_t id, size_t count) {
	// TODO
}

void urlOpenMatch(size_t id, const char *str) {
	// TODO
}
