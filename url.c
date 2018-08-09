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

#include <assert.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "chat.h"

static const char *SCHEMES[] = {
	"https:",
	"http:",
	"ftp:",
};
static const size_t SCHEMES_LEN = sizeof(SCHEMES) / sizeof(SCHEMES[0]);

enum { RING_LEN = 16 };
static char *ring[RING_LEN];
static size_t last;
static_assert(!(RING_LEN & (RING_LEN - 1)), "power of two RING_LEN");

static void push(const char *url, size_t len) {
	free(ring[last]);
	ring[last++] = strndup(url, len);
	last &= RING_LEN - 1;
}

void urlScan(const char *str) {
	while (str[0]) {
		size_t len = 1;
		for (size_t i = 0; i < SCHEMES_LEN; ++i) {
			if (strncmp(str, SCHEMES[i], strlen(SCHEMES[i]))) continue;
			len = strcspn(str, " >\"");
			push(str, len);
		}
		str = &str[len];
	}
}

void urlList(void) {
	uiHide();
	for (size_t i = 0; i < RING_LEN; ++i) {
		char *url = ring[(i + last) & (RING_LEN - 1)];
		if (url) printf("%s\n", url);
	}
}

void urlOpen(size_t i) {
	char *url = ring[(last - i) & (RING_LEN - 1)];
	if (!url) return;

	int fd[2];
	int error = pipe(fd);
	if (error) err(EX_OSERR, "pipe");

	pid_t pid = fork();
	if (pid < 0) err(EX_OSERR, "fork");

	if (!pid) {
		close(STDIN_FILENO);
		dup2(fd[1], STDOUT_FILENO);
		dup2(fd[1], STDERR_FILENO);
		execlp("open", "open", url, NULL);
		perror("open");
		exit(EX_CONFIG);
	}
	close(fd[1]);

	// FIXME: This should technically go on the main event loop.
	char buf[256];
	ssize_t len = read(fd[0], buf, sizeof(buf) - 1);
	if (len < 0) err(EX_IOERR, "read");
	if (len) {
		buf[len] = '\0';
		len = strcspn(buf, "\n");
		uiFmt("%.*s", (int)len, buf);
	}
	close(fd[0]);
}
