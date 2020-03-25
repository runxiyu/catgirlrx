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
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sysexits.h>
#include <time.h>

#include "chat.h"

bool logEnable;

static struct {
	int year;
	int month;
	int day;
	FILE *file;
} logs[IDCap];

static FILE *logFile(uint id, const struct tm *tm) {
	if (
		logs[id].file &&
		logs[id].year == tm->tm_year &&
		logs[id].month == tm->tm_mon &&
		logs[id].day == tm->tm_mday
	) return logs[id].file;

	if (logs[id].file) {
		int error = fclose(logs[id].file);
		if (error) err(EX_IOERR, "%s", idNames[id]);
	}

	logs[id].year = tm->tm_year;
	logs[id].month = tm->tm_mon;
	logs[id].day = tm->tm_mday;

	char path[PATH_MAX] = "log";
	size_t len = strlen(path);
	dataMkdir("");
	dataMkdir(path);

	path[len++] = '/';
	for (const char *ch = network.name; *ch; ++ch) {
		path[len++] = (*ch == '/' ? '_' : *ch);
	}
	path[len] = '\0';
	dataMkdir(path);

	path[len++] = '/';
	for (const char *ch = idNames[id]; *ch; ++ch) {
		path[len++] = (*ch == '/' ? '_' : *ch);
	}
	path[len] = '\0';
	dataMkdir(path);

	strftime(&path[len], sizeof(path) - len, "/%F.log", tm);
	logs[id].file = dataOpen(path, "a");
	if (!logs[id].file) exit(EX_CANTCREAT);

	setlinebuf(logs[id].file);
	return logs[id].file;
}

void logClose(void) {
	if (!logEnable) return;
	for (uint id = 0; id < IDCap; ++id) {
		if (!logs[id].file) continue;
		int error = fclose(logs[id].file);
		if (error) err(EX_IOERR, "%s", idNames[id]);
	}
}

void logFormat(uint id, const time_t *src, const char *format, ...) {
	if (!logEnable) return;

	time_t ts = (src ? *src : time(NULL));
	struct tm *tm = localtime(&ts);
	if (!tm) err(EX_OSERR, "localtime");

	FILE *file = logFile(id, tm);

	char buf[sizeof("0000-00-00T00:00:00+0000")];
	strftime(buf, sizeof(buf), "%FT%T%z", tm);
	fprintf(file, "[%s] ", buf);
	if (ferror(file)) err(EX_IOERR, "%s", idNames[id]);

	va_list ap;
	va_start(ap, format);
	vfprintf(file, format, ap);
	va_end(ap);
	if (ferror(file)) err(EX_IOERR, "%s", idNames[id]);

	fprintf(file, "\n");
	if (ferror(file)) err(EX_IOERR, "%s", idNames[id]);
}
