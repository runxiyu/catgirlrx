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
#include <errno.h>
#include <limits.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "chat.h"

static const char *Pattern = {
	"("
	"cvs|"
	"ftp|"
	"gemini|"
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
	":([^[:space:]>\"()]|[(][^)]*[)])+"
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
	errx(1, "regcomp: %s: %s", buf, Pattern);
}

struct URL {
	uint id;
	char *nick;
	char *url;
};

enum { Cap = 64 };
static struct {
	struct URL urls[Cap];
	size_t len;
} ring;
_Static_assert(!(Cap & (Cap - 1)), "Cap is power of two");

static void push(uint id, const char *nick, const char *str, size_t len) {
	struct URL *url = &ring.urls[ring.len++ % Cap];
	free(url->nick);
	free(url->url);

	url->id = id;
	url->nick = NULL;
	if (nick) {
		url->nick = strdup(nick);
		if (!url->nick) err(1, "strdup");
	}
	url->url = malloc(len + 1);
	if (!url->url) err(1, "malloc");

	char buf[1024];
	snprintf(buf, sizeof(buf), "%.*s", (int)len, str);
	styleStrip(url->url, len + 1, buf);
}

void urlScan(uint id, const char *nick, const char *mesg) {
	if (!mesg) return;
	compile();
	regmatch_t match = {0};
	for (const char *ptr = mesg; *ptr; ptr += match.rm_eo) {
		if (regexec(&Regex, ptr, 1, &match, 0)) break;
		push(id, nick, &ptr[match.rm_so], match.rm_eo - match.rm_so);
	}
}

struct Util urlOpenUtil;
static const struct Util OpenUtils[] = {
	{ 1, { "open" } },
	{ 1, { "xdg-open" } },
};

static void urlOpen(const char *url) {
	pid_t pid = fork();
	if (pid < 0) err(1, "fork");
	if (pid) return;

	setsid();
	close(STDIN_FILENO);
	dup2(utilPipe[1], STDOUT_FILENO);
	dup2(utilPipe[1], STDERR_FILENO);
	if (urlOpenUtil.argc) {
		struct Util util = urlOpenUtil;
		utilPush(&util, url);
		execvp(util.argv[0], (char *const *)util.argv);
		warn("%s", util.argv[0]);
		_exit(127);
	}
	for (size_t i = 0; i < ARRAY_LEN(OpenUtils); ++i) {
		struct Util util = OpenUtils[i];
		utilPush(&util, url);
		execvp(util.argv[0], (char *const *)util.argv);
		if (errno != ENOENT) {
			warn("%s", util.argv[0]);
			_exit(127);
		}
	}
	warnx("no open utility found");
	_exit(127);
}

struct Util urlCopyUtil;
static const struct Util CopyUtils[] = {
	{ 1, { "pbcopy" } },
	{ 1, { "wl-copy" } },
	{ 3, { "xclip", "-selection", "clipboard" } },
	{ 3, { "xsel", "-i", "-b" } },
};

static void urlCopy(const char *url) {
	int rw[2];
	int error = pipe(rw);
	if (error) err(1, "pipe");

	size_t len = strlen(url);
	if (len > PIPE_BUF) len = PIPE_BUF;
	ssize_t n = write(rw[1], url, len);
	if (n < 0) err(1, "write");

	error = close(rw[1]);
	if (error) err(1, "close");

	pid_t pid = fork();
	if (pid < 0) err(1, "fork");
	if (pid) {
		close(rw[0]);
		return;
	}

	setsid();
	dup2(rw[0], STDIN_FILENO);
	dup2(utilPipe[1], STDOUT_FILENO);
	dup2(utilPipe[1], STDERR_FILENO);
	close(rw[0]);
	if (urlCopyUtil.argc) {
		execvp(urlCopyUtil.argv[0], (char *const *)urlCopyUtil.argv);
		warn("%s", urlCopyUtil.argv[0]);
		_exit(127);
	}
	for (size_t i = 0; i < ARRAY_LEN(CopyUtils); ++i) {
		execvp(CopyUtils[i].argv[0], (char *const *)CopyUtils[i].argv);
		if (errno != ENOENT) {
			warn("%s", CopyUtils[i].argv[0]);
			_exit(127);
		}
	}
	warnx("no copy utility found");
	_exit(127);
}

void urlOpenCount(uint id, uint count) {
	for (uint i = 1; i <= Cap; ++i) {
		const struct URL *url = &ring.urls[(ring.len - i) % Cap];
		if (!url->url) break;
		if (url->id != id) continue;
		urlOpen(url->url);
		if (!--count) break;
	}
}

void urlOpenMatch(uint id, const char *str) {
	for (uint i = 1; i <= Cap; ++i) {
		const struct URL *url = &ring.urls[(ring.len - i) % Cap];
		if (!url->url) break;
		if (url->id != id) continue;
		if ((url->nick && !strcmp(url->nick, str)) || strstr(url->url, str)) {
			urlOpen(url->url);
			break;
		}
	}
}

void urlCopyMatch(uint id, const char *str) {
	for (uint i = 1; i <= Cap; ++i) {
		const struct URL *url = &ring.urls[(ring.len - i) % Cap];
		if (!url->url) break;
		if (url->id != id) continue;
		if (
			!str
			|| (url->nick && !strcmp(url->nick, str))
			|| strstr(url->url, str)
		) {
			urlCopy(url->url);
			break;
		}
	}
}

static int writeString(FILE *file, const char *str) {
	return (fwrite(str, strlen(str) + 1, 1, file) ? 0 : -1);
}
static ssize_t readString(FILE *file, char **buf, size_t *cap) {
	ssize_t len = getdelim(buf, cap, '\0', file);
	if (len < 0 && !feof(file)) err(1, "getdelim");
	return len;
}

int urlSave(FILE *file) {
	for (size_t i = 0; i < Cap; ++i) {
		const struct URL *url = &ring.urls[(ring.len + i) % Cap];
		if (!url->url) continue;
		int error = 0
			|| writeString(file, idNames[url->id])
			|| writeString(file, (url->nick ?: ""))
			|| writeString(file, url->url);
		if (error) return error;
	}
	return writeString(file, "");
}

void urlLoad(FILE *file, size_t version) {
	if (version < 5) return;
	size_t cap = 0;
	char *buf = NULL;
	while (0 < readString(file, &buf, &cap) && buf[0]) {
		struct URL *url = &ring.urls[ring.len++ % Cap];
		free(url->nick);
		free(url->url);
		url->id = idFor(buf);
		url->nick = NULL;
		readString(file, &buf, &cap);
		if (buf[0]) {
			url->nick = strdup(buf);
			if (!url->nick) err(1, "strdup");
		}
		readString(file, &buf, &cap);
		url->url = strdup(buf);
		if (!url->url) err(1, "strdup");
	}
	free(buf);
}
