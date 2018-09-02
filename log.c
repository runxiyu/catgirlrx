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
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sysexits.h>
#include <time.h>

#include "chat.h"

static int logRoot = -1;

static struct Log {
	int dir;
	int year;
	int month;
	int day;
	FILE *file;
} logs[TagsLen];

void logOpen(const char *path) {
	logRoot = open(path, O_RDONLY | O_CLOEXEC);
	if (logRoot < 0) err(EX_CANTCREAT, "%s", path);
}

static void sanitize(char *name) {
	for (; name[0]; ++name) {
		if (name[0] == '/') name[0] = '_';
	}
}

static FILE *logFile(struct Tag tag, const struct tm *time) {
	struct Log *log = &logs[tag.id];
	if (
		log->file
		&& log->year == time->tm_year
		&& log->month == time->tm_mon
		&& log->day == time->tm_mday
	) return log->file;

	if (log->file) {
		fclose(log->file);

	} else {
		char *name = strdup(tag.name);
		if (!name) err(EX_OSERR, "strdup");
		sanitize(name);

		int error = mkdirat(logRoot, name, 0700);
		if (error && errno != EEXIST) err(EX_CANTCREAT, "%s", name);

		log->dir = openat(logRoot, name, O_RDONLY | O_CLOEXEC);
		if (log->dir < 0) err(EX_CANTCREAT, "%s", name);

		free(name);
	}

	log->year = time->tm_year;
	log->month = time->tm_mon;
	log->day = time->tm_mday;

	char path[sizeof("YYYY-MM-DD.log")];
	strftime(path, sizeof(path), "%F.log", time);
	int fd = openat(
		log->dir, path, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0600
	);
	if (fd < 0) err(EX_CANTCREAT, "%s/%s", tag.name, path);

	log->file = fdopen(fd, "a");
	if (!log->file) err(EX_CANTCREAT, "%s/%s", tag.name, path);
	setlinebuf(log->file);

	return log->file;
}

void logFmt(struct Tag tag, const time_t *ts, const char *format, ...) {
	if (logRoot < 0) return;

	time_t t;
	if (!ts) {
		t = time(NULL);
		ts = &t;
	}

	struct tm *time = localtime(ts);
	if (!time) err(EX_SOFTWARE, "localtime");

	FILE *file = logFile(tag, time);

	char stamp[sizeof("YYYY-MM-DDThh:mm:ss+hhmm")];
	strftime(stamp, sizeof(stamp), "%FT%T%z", time);
	fprintf(file, "[%s] ", stamp);
	if (ferror(file)) err(EX_IOERR, "%s", tag.name);

	va_list ap;
	va_start(ap, format);
	vfprintf(file, format, ap);
	va_end(ap);
	if (ferror(file)) err(EX_IOERR, "%s", tag.name);

	fprintf(file, "\n");
	if (ferror(file)) err(EX_IOERR, "%s", tag.name);
}
