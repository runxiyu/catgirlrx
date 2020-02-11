/* Copyright (C) 2019, 2020  C. McEnroe <june@causal.agency>
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
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "chat.h"

#define SUBDIR "catgirl"

FILE *configOpen(const char *path, const char *mode) {
	if (path[0] == '/' || path[0] == '.') goto local;

	const char *home = getenv("HOME");
	const char *configHome = getenv("XDG_CONFIG_HOME");
	const char *configDirs = getenv("XDG_CONFIG_DIRS");

	char buf[PATH_MAX];
	if (configHome) {
		snprintf(buf, sizeof(buf), "%s/" SUBDIR "/%s", configHome, path);
	} else {
		if (!home) goto local;
		snprintf(buf, sizeof(buf), "%s/.config/" SUBDIR "/%s", home, path);
	}
	FILE *file = fopen(buf, mode);
	if (file) return file;
	if (errno != ENOENT) {
		warn("%s", buf);
		return NULL;
	}

	if (!configDirs) configDirs = "/etc/xdg";
	while (*configDirs) {
		size_t len = strcspn(configDirs, ":");
		snprintf(
			buf, sizeof(buf), "%.*s/" SUBDIR "/%s",
			(int)len, configDirs, path
		);
		file = fopen(buf, mode);
		if (file) return file;
		if (errno != ENOENT) {
			warn("%s", buf);
			return NULL;
		}
		configDirs += len;
		if (*configDirs) configDirs++;
	}

local:
	file = fopen(path, mode);
	if (!file) warn("%s", path);
	return file;
}

FILE *dataOpen(const char *path, const char *mode) {
	if (path[0] == '/' || path[0] == '.') goto local;

	const char *home = getenv("HOME");
	const char *dataHome = getenv("XDG_DATA_HOME");
	const char *dataDirs = getenv("XDG_DATA_DIRS");

	char homePath[PATH_MAX];
	if (dataHome) {
		snprintf(
			homePath, sizeof(homePath),
			"%s/" SUBDIR "/%s", dataHome, path
		);
	} else {
		if (!home) goto local;
		snprintf(
			homePath, sizeof(homePath),
			"%s/.local/share/" SUBDIR "/%s", home, path
		);
	}
	FILE *file = fopen(homePath, mode);
	if (file) return file;
	if (errno != ENOENT) {
		warn("%s", homePath);
		return NULL;
	}

	char buf[PATH_MAX];
	if (!dataDirs) dataDirs = "/usr/local/share:/usr/share";
	while (*dataDirs) {
		size_t len = strcspn(dataDirs, ":");
		snprintf(
			buf, sizeof(buf), "%.*s/" SUBDIR "/%s",
			(int)len, dataDirs, path
		);
		file = fopen(buf, mode);
		if (file) return file;
		if (errno != ENOENT) {
			warn("%s", buf);
			return NULL;
		}
		dataDirs += len;
		if (*dataDirs) dataDirs++;
	}

	if (mode[0] != 'r') {
		char *base = strrchr(homePath, '/');
		*base = '\0';
		int error = mkdir(homePath, S_IRWXU);
		if (error && errno != EEXIST) {
			warn("%s", homePath);
			return NULL;
		}
		*base = '/';
		file = fopen(homePath, mode);
		if (!file) warn("%s", homePath);
		return file;
	}

local:
	file = fopen(path, mode);
	if (!file) warn("%s", path);
	return file;
}
