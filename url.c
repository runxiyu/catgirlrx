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
#include <err.h>
#include <errno.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

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

struct URL {
	size_t id;
	char *nick;
	char *url;
};

enum { Cap = 32 };
static struct {
	struct URL urls[Cap];
	size_t len;
} ring;
static_assert(!(Cap & (Cap - 1)), "Cap is power of two");

static void push(size_t id, const char *nick, const char *str, size_t len) {
	struct URL *url = &ring.urls[ring.len++ % Cap];
	free(url->nick);
	free(url->url);
	url->id = id;
	url->nick = NULL;
	if (nick) {
		url->nick = strdup(nick);
		if (!url->nick) err(EX_OSERR, "strdup");
	}
	url->url = strndup(str, len);
	if (!url->url) err(EX_OSERR, "strndup");
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

static const char *OpenBins[] = { "open", "xdg-open" };

static void urlOpen(const char *url) {
	pid_t pid = fork();
	if (pid < 0) err(EX_OSERR, "fork");
	if (pid) return;

	close(STDIN_FILENO);
	dup2(procPipe[1], STDOUT_FILENO);
	dup2(procPipe[1], STDERR_FILENO);
	for (size_t i = 0; i < ARRAY_LEN(OpenBins); ++i) {
		execlp(OpenBins[i], OpenBins[i], url, NULL);
		if (errno != ENOENT) {
			warn("%s", OpenBins[i]);
			_exit(EX_CONFIG);
		}
	}
	warnx("no open utility found");
	_exit(EX_CONFIG);
}

void urlOpenCount(size_t id, size_t count) {
	for (size_t i = 1; i <= Cap; ++i) {
		const struct URL *url = &ring.urls[(ring.len - i) % Cap];
		if (!url->url) break;
		if (url->id != id) continue;
		urlOpen(url->url);
		if (!--count) break;
	}
}

void urlOpenMatch(size_t id, const char *str) {
	for (size_t i = 1; i <= Cap; ++i) {
		const struct URL *url = &ring.urls[(ring.len - i) % Cap];
		if (!url->url) break;
		if (url->id != id) continue;
		if ((url->nick && !strcmp(url->nick, str)) || strstr(url->url, str)) {
			urlOpen(url->url);
			break;
		}
	}
}
