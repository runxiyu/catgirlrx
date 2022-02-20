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

#define _XOPEN_SOURCE_EXTENDED

#include <curses.h>
#include <err.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sysexits.h>
#include <termios.h>

#include "chat.h"
#include "edit.h"

#define ENUM_KEY \
	X(KeyCtrlLeft, "\33[1;5D", NULL) \
	X(KeyCtrlRight, "\33[1;5C", NULL) \
	X(KeyMeta0, "\0330", "\33)") \
	X(KeyMeta1, "\0331", "\33!") \
	X(KeyMeta2, "\0332", "\33@") \
	X(KeyMeta3, "\0333", "\33#") \
	X(KeyMeta4, "\0334", "\33$") \
	X(KeyMeta5, "\0335", "\33%") \
	X(KeyMeta6, "\0336", "\33^") \
	X(KeyMeta7, "\0337", "\33&") \
	X(KeyMeta8, "\0338", "\33*") \
	X(KeyMeta9, "\0339", "\33(") \
	X(KeyMetaA, "\33a", NULL) \
	X(KeyMetaB, "\33b", NULL) \
	X(KeyMetaD, "\33d", NULL) \
	X(KeyMetaF, "\33f", NULL) \
	X(KeyMetaL, "\33l", NULL) \
	X(KeyMetaM, "\33m", NULL) \
	X(KeyMetaN, "\33n", NULL) \
	X(KeyMetaP, "\33p", NULL) \
	X(KeyMetaQ, "\33q", NULL) \
	X(KeyMetaS, "\33s", NULL) \
	X(KeyMetaT, "\33t", NULL) \
	X(KeyMetaU, "\33u", NULL) \
	X(KeyMetaV, "\33v", NULL) \
	X(KeyMetaEnter, "\33\r", "\33\n") \
	X(KeyMetaGt, "\33>", "\33.") \
	X(KeyMetaLt, "\33<", "\33,") \
	X(KeyMetaEqual, "\33=", NULL) \
	X(KeyMetaMinus, "\33-", "\33_") \
	X(KeyMetaPlus, "\33+", NULL) \
	X(KeyMetaSlash, "\33/", "\33?") \
	X(KeyFocusIn, "\33[I", NULL) \
	X(KeyFocusOut, "\33[O", NULL) \
	X(KeyPasteOn, "\33[200~", NULL) \
	X(KeyPasteOff, "\33[201~", NULL) \
	X(KeyPasteManual, "\32p", "\32\20")

enum {
	KeyMax = KEY_MAX,
#define X(id, seq, alt) id,
	ENUM_KEY
#undef X
};

static struct Edit edit;

void inputInit(void) {
	struct termios term;
	int error = tcgetattr(STDOUT_FILENO, &term);
	if (error) err(EX_OSERR, "tcgetattr");

	// Gain use of C-q, C-s, C-c, C-z, C-y, C-v, C-o.
	term.c_iflag &= ~IXON;
	term.c_cc[VINTR] = _POSIX_VDISABLE;
	term.c_cc[VSUSP] = _POSIX_VDISABLE;
#ifdef VDSUSP
	term.c_cc[VDSUSP] = _POSIX_VDISABLE;
#endif
	term.c_cc[VLNEXT] = _POSIX_VDISABLE;
	term.c_cc[VDISCARD] = _POSIX_VDISABLE;

	error = tcsetattr(STDOUT_FILENO, TCSANOW, &term);
	if (error) err(EX_OSERR, "tcsetattr");

	def_prog_mode();

#define X(id, seq, alt) define_key(seq, id); if (alt) define_key(alt, id);
	ENUM_KEY
#undef X

	keypad(uiInput, true);
	nodelay(uiInput, true);
}

static void inputAdd(struct Style reset, struct Style *style, const char *str) {
	while (*str) {
		const char *code = str;
		size_t len = styleParse(style, &str);
		wattr_set(uiInput, A_BOLD | A_REVERSE, 0, NULL);
		switch (*code) {
			break; case B: waddch(uiInput, 'B');
			break; case C: waddch(uiInput, 'C');
			break; case O: waddch(uiInput, 'O');
			break; case R: waddch(uiInput, 'R');
			break; case I: waddch(uiInput, 'I');
			break; case U: waddch(uiInput, 'U');
			break; case '\n': waddch(uiInput, 'N');
		}
		if (str - code > 1) waddnstr(uiInput, &code[1], str - &code[1]);
		if (str[0] == '\n') {
			*style = reset;
			str++;
			len--;
		}
		size_t nl = strcspn(str, "\n");
		if (nl < len) len = nl;
		wattr_set(uiInput, uiAttr(*style), uiPair(*style), NULL);
		waddnstr(uiInput, str, len);
		str += len;
	}
}

static char *inputStop(
	struct Style reset, struct Style *style,
	const char *str, char *stop
) {
	char ch = *stop;
	*stop = '\0';
	inputAdd(reset, style, str);
	*stop = ch;
	return stop;
}

void inputUpdate(void) {
	uint id = windowID();
	char *buf = editString(&edit);

	const char *prefix = "";
	const char *prompt = self.nick;
	const char *suffix = "";
	const char *skip = buf;
	struct Style stylePrompt = { .fg = self.color, .bg = Default };
	struct Style styleInput = StyleDefault;

	size_t split = commandWillSplit(id, buf);
	const char *privmsg = commandIsPrivmsg(id, buf);
	const char *notice = commandIsNotice(id, buf);
	const char *action = commandIsAction(id, buf);
	if (privmsg) {
		prefix = "<"; suffix = "> ";
		skip = privmsg;
	} else if (notice) {
		prefix = "-"; suffix = "- ";
		styleInput.fg = LightGray;
		skip = notice;
	} else if (action) {
		prefix = "* "; suffix = " ";
		stylePrompt.attr |= Italic;
		styleInput.attr |= Italic;
		skip = action;
	} else if (id == Debug && buf[0] != '/') {
		prompt = "<< ";
		stylePrompt.fg = Gray;
	} else {
		prompt = "";
	}
	if (skip > &buf[edit.mbs.pos]) {
		prefix = prompt = suffix = "";
		skip = buf;
	}

	int y, x;
	wmove(uiInput, 0, 0);
	if (windowTimeEnable() && id != Network) {
		whline(uiInput, ' ', windowTime.width);
		wmove(uiInput, 0, windowTime.width);
	}
	wattr_set(uiInput, uiAttr(stylePrompt), uiPair(stylePrompt), NULL);
	waddstr(uiInput, prefix);
	waddstr(uiInput, prompt);
	waddstr(uiInput, suffix);
	getyx(uiInput, y, x);

	int pos;
	struct Style style = styleInput;
	inputStop(styleInput, &style, skip, &buf[edit.mbs.pos]);
	getyx(uiInput, y, pos);
	wmove(uiInput, y, x);

	style = styleInput;
	const char *ptr = skip;
	if (split) {
		ptr = inputStop(styleInput, &style, ptr, &buf[split]);
		style = styleInput;
		style.bg = Red;
	}
	inputAdd(styleInput, &style, ptr);
	wclrtoeol(uiInput);
	wmove(uiInput, y, pos);
}

static void inputEnter(void) {
	command(windowID(), editString(&edit));
	editFn(&edit, EditClear);
}

static void keyCode(int code) {
	switch (code) {
		break; case KEY_RESIZE:  uiResize();
		break; case KeyFocusIn:  windowUnmark();
		break; case KeyFocusOut: windowMark();

		break; case KeyMetaEnter: editInsert(&edit, L'\n');
		break; case KeyMetaEqual: windowToggleMute();
		break; case KeyMetaMinus: windowToggleThresh(-1);
		break; case KeyMetaPlus:  windowToggleThresh(+1);
		break; case KeyMetaSlash: windowSwap();

		break; case KeyMetaGt: windowScroll(ScrollAll, -1);
		break; case KeyMetaLt: windowScroll(ScrollAll, +1);

		break; case KeyMeta0 ... KeyMeta9: windowShow(code - KeyMeta0);
		break; case KeyMetaA: windowAuto();
		break; case KeyMetaB: editFn(&edit, EditPrevWord);
		break; case KeyMetaD: editFn(&edit, EditDeleteNextWord);
		break; case KeyMetaF: editFn(&edit, EditNextWord);
		break; case KeyMetaL: windowBare();
		break; case KeyMetaM: uiWrite(windowID(), Warm, NULL, "");
		break; case KeyMetaN: windowScroll(ScrollHot, +1);
		break; case KeyMetaP: windowScroll(ScrollHot, -1);
		break; case KeyMetaQ: editFn(&edit, EditCollapse);
		break; case KeyMetaS: uiSpoilerReveal ^= true; windowUpdate();
		break; case KeyMetaT: windowToggleTime();
		break; case KeyMetaU: windowScroll(ScrollUnread, 0);
		break; case KeyMetaV: windowScroll(ScrollPage, +1);

		break; case KeyCtrlLeft: editFn(&edit, EditPrevWord);
		break; case KeyCtrlRight: editFn(&edit, EditNextWord);

		break; case KEY_BACKSPACE: editFn(&edit, EditDeletePrev);
		break; case KEY_DC: editFn(&edit, EditDeleteNext);
		break; case KEY_DOWN: windowScroll(ScrollOne, -1);
		break; case KEY_END: editFn(&edit, EditTail);
		break; case KEY_ENTER: inputEnter();
		break; case KEY_HOME: editFn(&edit, EditHead);
		break; case KEY_LEFT: editFn(&edit, EditPrev);
		break; case KEY_NPAGE: windowScroll(ScrollPage, -1);
		break; case KEY_PPAGE: windowScroll(ScrollPage, +1);
		break; case KEY_RIGHT: editFn(&edit, EditNext);
		break; case KEY_SEND: windowScroll(ScrollAll, -1);
		break; case KEY_SHOME: windowScroll(ScrollAll, +1);
		break; case KEY_UP: windowScroll(ScrollOne, +1);
	}
}

static void keyCtrl(wchar_t ch) {
	switch (ch ^ L'@') {
		break; case L'?': editFn(&edit, EditDeletePrev);
		break; case L'A': editFn(&edit, EditHead);
		break; case L'B': editFn(&edit, EditPrev);
		break; case L'C': raise(SIGINT);
		break; case L'D': editFn(&edit, EditDeleteNext);
		break; case L'E': editFn(&edit, EditTail);
		break; case L'F': editFn(&edit, EditNext);
		break; case L'H': editFn(&edit, EditDeletePrev);
		break; case L'J': inputEnter();
		break; case L'K': editFn(&edit, EditDeleteTail);
		break; case L'L': clearok(curscr, true);
		break; case L'N': windowShow(windowNum() + 1);
		break; case L'P': windowShow(windowNum() - 1);
		break; case L'R': windowSearch(editString(&edit), -1);
		break; case L'S': windowSearch(editString(&edit), +1);
		break; case L'T': editFn(&edit, EditTranspose);
		break; case L'U': editFn(&edit, EditDeleteHead);
		break; case L'V': windowScroll(ScrollPage, -1);
		break; case L'W': editFn(&edit, EditDeletePrevWord);
		break; case L'Y': editFn(&edit, EditPaste);
	}
}

static void keyStyle(wchar_t ch) {
	if (iswcntrl(ch)) ch = towlower(ch ^ L'@');
	char buf[8] = {0};
	enum Color color = Default;
	switch (ch) {
		break; case L'A': color = Gray;
		break; case L'B': color = Blue;
		break; case L'C': color = Cyan;
		break; case L'G': color = Green;
		break; case L'K': color = Black;
		break; case L'M': color = Magenta;
		break; case L'N': color = Brown;
		break; case L'O': color = Orange;
		break; case L'P': color = Pink;
		break; case L'R': color = Red;
		break; case L'W': color = White;
		break; case L'Y': color = Yellow;
		break; case L'b': buf[0] = B;
		break; case L'c': buf[0] = C;
		break; case L'i': buf[0] = I;
		break; case L'o': buf[0] = O;
		break; case L'r': buf[0] = R;
		break; case L's': {
			snprintf(buf, sizeof(buf), "%c%02d,%02d", C, Black, Black);
		}
		break; case L'u': buf[0] = U;
	}
	if (color != Default) {
		snprintf(buf, sizeof(buf), "%c%02d", C, color);
	}
	for (char *ch = buf; *ch; ++ch) {
		editInsert(&edit, *ch);
	}
}

static bool waiting;

void inputWait(void) {
	waiting = true;
}

void inputRead(void) {
	if (isendwin()) {
		if (waiting) {
			uiShow();
			flushinp();
			waiting = false;
		} else {
			return;
		}
	}

	wint_t ch;
	static bool paste, style, literal;
	for (int ret; ERR != (ret = wget_wch(uiInput, &ch));) {
		bool spr = uiSpoilerReveal;
		if (ret == KEY_CODE_YES && ch == KeyPasteOn) {
			paste = true;
		} else if (ret == KEY_CODE_YES && ch == KeyPasteOff) {
			paste = false;
		} else if (ret == KEY_CODE_YES && ch == KeyPasteManual) {
			paste ^= true;
		} else if (paste || literal) {
			editInsert(&edit, ch);
		} else if (ret == KEY_CODE_YES) {
			keyCode(ch);
		} else if (ch == (L'Z' ^ L'@')) {
			style = true;
			continue;
		} else if (style && ch == (L'V' ^ L'@')) {
			literal = true;
			continue;
		} else if (style) {
			keyStyle(ch);
		} else if (iswcntrl(ch)) {
			keyCtrl(ch);
		} else {
			editInsert(&edit, ch);
		}
		style = false;
		literal = false;
		if (spr) {
			uiSpoilerReveal = false;
			windowUpdate();
		}
	}
	inputUpdate();
}
