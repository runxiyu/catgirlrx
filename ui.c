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
 * combining it with OpenSSL (or a modified version of that library),
 * containing parts covered by the terms of the OpenSSL License and the
 * original SSLeay license, the licensors of this Program grant you
 * additional permission to convey the resulting work. Corresponding
 * Source for a non-source form of such a combination shall include the
 * source code for the parts of OpenSSL used as well as that of the
 * covered work.
 */

#define _XOPEN_SOURCE_EXTENDED

#include <assert.h>
#include <curses.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sysexits.h>
#include <term.h>
#include <time.h>
#include <unistd.h>

#ifdef __FreeBSD__
#include <capsicum_helpers.h>
#endif

#include "chat.h"

#define BOTTOM (LINES - 1)
#define RIGHT (COLS - 1)
#define MAIN_LINES (LINES - StatusLines - InputLines)

WINDOW *uiStatus;
WINDOW *uiMain;
WINDOW *uiInput;

static short colorPairs;

static void colorInit(void) {
	start_color();
	use_default_colors();
	if (!COLORS) return;
	for (short pair = 0; pair < 16; ++pair) {
		init_pair(1 + pair, pair % COLORS, -1);
	}
	colorPairs = 17;
}

static attr_t colorAttr(short fg) {
	if (!COLORS) return (fg > 0 ? A_BOLD : A_NORMAL);
	if (fg != COLOR_BLACK && fg % COLORS == COLOR_BLACK) return A_BOLD;
	if (COLORS > 8) return A_NORMAL;
	return (fg / COLORS & 1 ? A_BOLD : A_NORMAL);
}

static short colorPair(short fg, short bg) {
	if (!COLORS) return 0;
	fg %= COLORS;
	bg %= COLORS;
	if (bg == -1 && fg < 16) return 1 + fg;
	for (short pair = 17; pair < colorPairs; ++pair) {
		short f, b;
		pair_content(pair, &f, &b);
		if (f == fg && b == bg) return pair;
	}
	init_pair(colorPairs, fg, bg);
	return colorPairs++;
}

// XXX: Assuming terminals will be fine with these even if they're unsupported,
// since they're "private" modes.
static const char *FocusMode[2] = { "\33[?1004l", "\33[?1004h" };
static const char *PasteMode[2] = { "\33[?2004l", "\33[?2004h" };

static void errExit(void) {
	putp(FocusMode[false]);
	putp(PasteMode[false]);
	reset_shell_mode();
}

void uiInit(void) {
	initscr();
	cbreak();
	noecho();
	colorInit();
	atexit(errExit);

#ifndef A_ITALIC
#define A_ITALIC A_BLINK
	// Force ncurses to use individual enter_attr_mode strings:
	set_attributes = NULL;
	enter_blink_mode = enter_italics_mode;
#endif

	if (!to_status_line && !strncmp(termname(), "xterm", 5)) {
		to_status_line = "\33]2;";
		from_status_line = "\7";
	}

	uiStatus = newwin(StatusLines, COLS, 0, 0);
	if (!uiStatus) err(EX_OSERR, "newwin");

	uiMain = newwin(MAIN_LINES, COLS, StatusLines, 0);
	if (!uiMain) err(EX_OSERR, "newwin");

	uiInput = newpad(InputLines, InputCols);
	if (!uiInput) err(EX_OSERR, "newpad");

	windowInit();
	uiShow();
}

static bool hidden = true;

char uiTitle[TitleCap];
static char prevTitle[TitleCap];

void uiDraw(void) {
	if (hidden) return;
	wnoutrefresh(uiStatus);
	wnoutrefresh(uiMain);
	int y, x;
	getyx(uiInput, y, x);
	pnoutrefresh(
		uiInput,
		0, (x + 1 > RIGHT ? x + 1 - RIGHT : 0),
		LINES - InputLines, 0,
		BOTTOM, RIGHT
	);
	(void)y;
	doupdate();

	if (!to_status_line) return;
	if (!strcmp(uiTitle, prevTitle)) return;
	strcpy(prevTitle, uiTitle);
	putp(to_status_line);
	putp(uiTitle);
	putp(from_status_line);
	fflush(stdout);
}

static const short Colors[ColorCap] = {
	[Default]    = -1,
	[White]      = 8 + COLOR_WHITE,
	[Black]      = 0 + COLOR_BLACK,
	[Blue]       = 0 + COLOR_BLUE,
	[Green]      = 0 + COLOR_GREEN,
	[Red]        = 8 + COLOR_RED,
	[Brown]      = 0 + COLOR_RED,
	[Magenta]    = 0 + COLOR_MAGENTA,
	[Orange]     = 0 + COLOR_YELLOW,
	[Yellow]     = 8 + COLOR_YELLOW,
	[LightGreen] = 8 + COLOR_GREEN,
	[Cyan]       = 0 + COLOR_CYAN,
	[LightCyan]  = 8 + COLOR_CYAN,
	[LightBlue]  = 8 + COLOR_BLUE,
	[Pink]       = 8 + COLOR_MAGENTA,
	[Gray]       = 8 + COLOR_BLACK,
	[LightGray]  = 0 + COLOR_WHITE,
	52, 94, 100, 58, 22, 29, 23, 24, 17, 54, 53, 89,
	88, 130, 142, 64, 28, 35, 30, 25, 18, 91, 90, 125,
	124, 166, 184, 106, 34, 49, 37, 33, 19, 129, 127, 161,
	196, 208, 226, 154, 46, 86, 51, 75, 21, 171, 201, 198,
	203, 215, 227, 191, 83, 122, 87, 111, 63, 177, 207, 205,
	217, 223, 229, 193, 157, 158, 159, 153, 147, 183, 219, 212,
	16, 233, 235, 237, 239, 241, 244, 247, 250, 254, 231,
};

uint uiAttr(struct Style style) {
	attr_t attr = A_NORMAL;
	if (style.attr & Bold) attr |= A_BOLD;
	if (style.attr & Reverse) attr |= A_REVERSE;
	if (style.attr & Italic) attr |= A_ITALIC;
	if (style.attr & Underline) attr |= A_UNDERLINE;
	return attr | colorAttr(Colors[style.fg]);
}

bool uiSpoilerReveal;

short uiPair(struct Style style) {
	if (uiSpoilerReveal && style.fg == style.bg) {
		return colorPair(Colors[Default], Colors[style.bg]);
	}
	return colorPair(Colors[style.fg], Colors[style.bg]);
}

void uiShow(void) {
	if (!hidden) return;
	prevTitle[0] = '\0';
	putp(FocusMode[true]);
	putp(PasteMode[true]);
	fflush(stdout);
	hidden = false;
	windowUnmark();
}

void uiHide(void) {
	if (hidden) return;
	windowMark();
	hidden = true;
	putp(FocusMode[false]);
	putp(PasteMode[false]);
	endwin();
}

struct Util uiNotifyUtil;
static void notify(uint id, const char *str) {
	if (self.restricted) return;
	if (!uiNotifyUtil.argc) return;

	char buf[1024];
	styleStrip(buf, sizeof(buf), str);

	struct Util util = uiNotifyUtil;
	utilPush(&util, idNames[id]);
	utilPush(&util, buf);

	pid_t pid = fork();
	if (pid < 0) err(EX_OSERR, "fork");
	if (pid) return;

	setsid();
	close(STDIN_FILENO);
	dup2(utilPipe[1], STDOUT_FILENO);
	dup2(utilPipe[1], STDERR_FILENO);
	execvp(util.argv[0], (char *const *)util.argv);
	warn("%s", util.argv[0]);
	_exit(EX_CONFIG);
}

void uiWrite(uint id, enum Heat heat, const time_t *src, const char *str) {
	bool note = windowWrite(id, heat, src, str);
	if (note) {
		beep();
		notify(id, str);
	}
}

void uiFormat(
	uint id, enum Heat heat, const time_t *time, const char *format, ...
) {
	char buf[1024];
	va_list ap;
	va_start(ap, format);
	int len = vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);
	assert((size_t)len < sizeof(buf));
	uiWrite(id, heat, time, buf);
}

void uiResize(void) {
	wclear(uiMain);
	wresize(uiMain, MAIN_LINES, COLS);
	windowResize();
}

static FILE *saveFile;

static const uint64_t Signatures[] = {
	0x6C72696774616301, // no heat, unread, unreadWarm
	0x6C72696774616302, // no self.pos
	0x6C72696774616303, // no buffer line heat
	0x6C72696774616304, // no mute
	0x6C72696774616305, // no URLs
	0x6C72696774616306, // no thresh
	0x6C72696774616307, // no window time
	0x6C72696774616308,
};

static size_t signatureVersion(uint64_t signature) {
	for (size_t i = 0; i < ARRAY_LEN(Signatures); ++i) {
		if (signature == Signatures[i]) return i;
	}
	errx(EX_DATAERR, "unknown file signature %" PRIX64, signature);
}

static int writeUint64(FILE *file, uint64_t u) {
	return (fwrite(&u, sizeof(u), 1, file) ? 0 : -1);
}

int uiSave(void) {
	return 0
		|| ftruncate(fileno(saveFile), 0)
		|| writeUint64(saveFile, Signatures[7])
		|| writeUint64(saveFile, self.pos)
		|| windowSave(saveFile)
		|| urlSave(saveFile)
		|| fclose(saveFile);
}

static uint64_t readUint64(FILE *file) {
	uint64_t u;
	fread(&u, sizeof(u), 1, file);
	if (ferror(file)) err(EX_IOERR, "fread");
	if (feof(file)) errx(EX_DATAERR, "unexpected eof");
	return u;
}

void uiLoad(const char *name) {
	int error;
	saveFile = dataOpen(name, "a+e");
	if (!saveFile) exit(EX_CANTCREAT);
	rewind(saveFile);

#ifdef __FreeBSD__
	cap_rights_t rights;
	cap_rights_init(&rights, CAP_READ, CAP_WRITE, CAP_FLOCK, CAP_FTRUNCATE);
	error = caph_rights_limit(fileno(saveFile), &rights);
	if (error) err(EX_OSERR, "cap_rights_limit");
#endif

	error = flock(fileno(saveFile), LOCK_EX | LOCK_NB);
	if (error && errno == EWOULDBLOCK) {
		errx(EX_CANTCREAT, "%s: save file in use", name);
	}

	time_t signature;
	fread(&signature, sizeof(signature), 1, saveFile);
	if (ferror(saveFile)) err(EX_IOERR, "fread");
	if (feof(saveFile)) {
		return;
	}
	size_t version = signatureVersion(signature);

	if (version > 1) {
		self.pos = readUint64(saveFile);
	}
	windowLoad(saveFile, version);
	urlLoad(saveFile, version);
}
