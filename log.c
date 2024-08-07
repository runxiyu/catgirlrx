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
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifdef __FreeBSD__
#include <capsicum_helpers.h>
#endif

#include "chat.h"

static int logDir = -1;

void logOpen(void) {
	char buf[PATH_MAX];
	int error = mkdir(dataPath(buf, sizeof(buf), "", 0), S_IRWXU);
	if (error && errno != EEXIST) err(1, "%s", buf);

	error = mkdir(dataPath(buf, sizeof(buf), "log", 0), S_IRWXU);
	if (error && errno != EEXIST) err(1, "%s", buf);

	logDir = open(buf, O_RDONLY | O_CLOEXEC);
	if (logDir < 0) err(1, "%s", buf);

#ifdef __FreeBSD__
	cap_rights_t rights;
	cap_rights_init(
		&rights, CAP_MKDIRAT, CAP_CREATE, CAP_WRITE,
		/* for fdopen(3) */ CAP_FCNTL, CAP_FSTAT
	);
	error = caph_rights_limit(logDir, &rights);
	if (error) err(1, "cap_rights_limit");
#endif
}

static void logMkdir(const char *path) {
	int error = mkdirat(logDir, path, S_IRWXU);
	if (error && errno != EEXIST) err(1, "log/%s", path);
}

static void sanitize(char *ptr, char *end) {
	for (char *ch = ptr; ch < end && *ch == '.'; ++ch) {
		*ch = '_';
	}
	for (char *ch = ptr; ch < end; ++ch) {
		if (*ch == '/') *ch = '_';
	}
}

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
		if (error) err(1, "%s", idNames[id]);
	}

	logs[id].year = tm->tm_year;
	logs[id].month = tm->tm_mon;
	logs[id].day = tm->tm_mday;

	char path[PATH_MAX];
	char *ptr = path, *end = &path[sizeof(path)];

	ptr = seprintf(ptr, end, "%s", network.name);
	sanitize(path, ptr);
	logMkdir(path);

	char *name = ptr;
	ptr = seprintf(ptr, end, "/%s", idNames[id]);
	sanitize(&name[1], ptr);
	logMkdir(path);

	size_t len = strftime(ptr, end - ptr, "/%F.log", tm);
	if (!len) errx(1, "log path too long");

	int fd = openat(
		logDir, path,
		O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC,
		S_IRUSR | S_IWUSR
	);
	if (fd < 0) err(1, "log/%s", path);
	logs[id].file = fdopen(fd, "a");
	if (!logs[id].file) err(1, "fdopen");

	setlinebuf(logs[id].file);
	return logs[id].file;
}

void logClose(void) {
	if (logDir < 0) return;
	for (uint id = 0; id < IDCap; ++id) {
		if (!logs[id].file) continue;
		int error = fclose(logs[id].file);
		if (error) err(1, "%s", idNames[id]);
	}
	close(logDir);
}

void logFormat(uint id, const time_t *src, const char *format, ...) {
	if (logDir < 0) return;

	time_t ts = (src ? *src : time(NULL));
	struct tm *tm = localtime(&ts);
	if (!tm) err(1, "localtime");

	FILE *file = logFile(id, tm);

	char buf[sizeof("0000-00-00T00:00:00+0000")];
	strftime(buf, sizeof(buf), "%FT%T%z", tm);
	int n = fprintf(file, "[%s] ", buf);
	if (n < 0) err(1, "%s", idNames[id]);

	va_list ap;
	va_start(ap, format);
	n = vfprintf(file, format, ap);
	va_end(ap);
	if (n < 0) err(1, "%s", idNames[id]);

	n = fprintf(file, "\n");
	if (n < 0) err(1, "%s", idNames[id]);
}
