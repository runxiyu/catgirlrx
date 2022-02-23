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

#include <assert.h>
#include <curses.h>
#include <err.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <termios.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

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

static struct Edit cut;
static struct Edit edits[IDCap];

void inputInit(void) {
	for (size_t i = 0; i < ARRAY_LEN(edits); ++i) {
		edits[i].cut = &cut;
	}

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

static size_t cap;
static char *buf;

void inputUpdate(void) {
	uint id = windowID();

	size_t pos = 0;
	const char *ptr = editString(&edits[id], &buf, &cap, &pos);
	if (!ptr) err(EX_OSERR, "editString");

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
	if (skip > &buf[pos]) {
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

	int posx;
	struct Style style = styleInput;
	inputStop(styleInput, &style, skip, &buf[pos]);
	getyx(uiInput, y, posx);
	wmove(uiInput, y, x);

	ptr = skip;
	style = styleInput;
	if (split) {
		ptr = inputStop(styleInput, &style, ptr, &buf[split]);
		style = styleInput;
		style.bg = Red;
	}
	inputAdd(styleInput, &style, ptr);
	wclrtoeol(uiInput);
	wmove(uiInput, y, posx);
}

bool inputPending(uint id) {
	return edits[id].len;
}

static const struct {
	const wchar_t *name;
	const wchar_t *string;
} Macros[] = {
	{ L"\\banhammer", L"▬▬▬▬▬▬▬▋ Ò╭╮Ó" },
	{ L"\\bear", L"ʕっ•ᴥ•ʔっ" },
	{ L"\\blush", L"（˶′◡‵˶）" },
	{ L"\\com", L"\0038,4\2 ☭ " },
	{ L"\\cool", L"(⌐■_■)" },
	{ L"\\flip", L"(╯°□°）╯︵ ┻━┻" },
	{ L"\\gary", L"ᕕ( ᐛ )ᕗ" },
	{ L"\\hug", L"（っ・∀・）っ" },
	{ L"\\lenny", L"( ͡° ͜ʖ ͡°)" },
	{ L"\\look", L"ಠ_ಠ" },
	{ L"\\shrug", L"¯\\_(ツ)_/¯" },
	{ L"\\unflip", L"┬─┬ノ(º_ºノ)" },
	{ L"\\wave", L"ヾ(＾∇＾)" },
};

void inputCompleteAdd(void) {
	char mbs[256];
	for (size_t i = 0; i < ARRAY_LEN(Macros); ++i) {
		size_t n = wcstombs(mbs, Macros[i].name, sizeof(mbs));
		assert(n != (size_t)-1);
		completeAdd(None, mbs, Default);
	}
}

static int macroExpand(struct Edit *e) {
	size_t macro = e->pos;
	while (macro && e->buf[macro] != L'\\') macro--;
	if (macro == e->pos) return 0;
	for (size_t i = 0; i < ARRAY_LEN(Macros); ++i) {
		if (wcslen(Macros[i].name) != e->pos - macro) continue;
		if (wcsncmp(Macros[i].name, &e->buf[macro], e->pos - macro)) continue;
		if (wcstombs(NULL, Macros[i].string, 0) == (size_t)-1) continue;
		size_t expand = wcslen(Macros[i].string);
		int error = 0
			|| editDelete(e, false, macro, e->pos - macro)
			|| editReserve(e, macro, expand);
		if (error) return error;
		wcsncpy(&e->buf[macro], Macros[i].string, expand);
		e->pos = macro + expand;
		break;
	}
	return 0;
}

static struct {
	uint id;
	char *pre;
	size_t pos;
	size_t len;
	bool suffix;
} tab;

static void tabAccept(void) {
	completeAccept();
	tab.len = 0;
}

static void tabReject(void) {
	completeReject();
	tab.len = 0;
}

static int tabComplete(struct Edit *e, uint id) {
	if (tab.len && id != tab.id) {
		tabAccept();
	}

	if (!tab.len) {
		tab.id = id;
		tab.pos = e->pos;
		while (tab.pos && !iswspace(e->buf[tab.pos-1])) tab.pos--;
		tab.len = e->pos - tab.pos;
		if (!tab.len) return 0;

		size_t cap = tab.len * MB_CUR_MAX + 1;
		char *buf = realloc(tab.pre, cap);
		if (!buf) return -1;
		tab.pre = buf;

		const wchar_t *ptr = &e->buf[tab.pos];
		size_t n = wcsnrtombs(tab.pre, &ptr, tab.len, cap-1, NULL);
		if (n == (size_t)-1) return -1;
		tab.pre[n] = '\0';
		tab.suffix = true;
	}

	const char *comp = complete(id, tab.pre);
	if (!comp) {
		comp = complete(id, tab.pre);
		tab.suffix ^= true;
	}
	if (!comp) {
		tab.len = 0;
		return 0;
	}

	size_t cap = strlen(comp) + 1;
	wchar_t *wcs = malloc(sizeof(*wcs) * cap);
	if (!wcs) return -1;

	size_t n = mbstowcs(wcs, comp, cap);
	assert(n != (size_t)-1);

	bool colon = (tab.len >= 2 && e->buf[tab.pos + tab.len - 2] == L':');

	int error = editDelete(e, false, tab.pos, tab.len);
	if (error) goto fail;

	tab.len = n;
	if (wcs[0] == L'\\' || wcschr(wcs, L' ')) {
		error = editReserve(e, tab.pos, tab.len);
		if (error) goto fail;
	} else if (wcs[0] != L'/' && tab.suffix && (!tab.pos || colon)) {
		tab.len += 2;
		error = editReserve(e, tab.pos, tab.len);
		if (error) goto fail;
		e->buf[tab.pos + n + 0] = L':';
		e->buf[tab.pos + n + 1] = L' ';
	} else if (tab.suffix && tab.pos >= 2 && e->buf[tab.pos - 2] == L':') {
		tab.len += 2;
		error = editReserve(e, tab.pos, tab.len);
		if (error) goto fail;
		e->buf[tab.pos - 2] = L',';
		e->buf[tab.pos + n + 0] = L':';
		e->buf[tab.pos + n + 1] = L' ';
	} else {
		tab.len++;
		error = editReserve(e, tab.pos, tab.len);
		if (error) goto fail;
		if (!tab.suffix && tab.pos >= 2 && e->buf[tab.pos - 2] == L',') {
			e->buf[tab.pos - 2] = L':';
		}
		e->buf[tab.pos + n] = L' ';
	}
	wmemcpy(&e->buf[tab.pos], wcs, n);
	e->pos = tab.pos + tab.len;
	free(wcs);
	return 0;

fail:
	free(wcs);
	return -1;
}

static void inputEnter(void) {
	uint id = windowID();
	char *cmd = editString(&edits[id], &buf, &cap, NULL);
	if (!cmd) err(EX_OSERR, "editString");

	tabAccept();
	editFn(&edits[id], EditClear);
	command(id, cmd);
}

static void keyCode(int code) {
	int error = 0;
	struct Edit *edit = &edits[windowID()];
	switch (code) {
		break; case KEY_RESIZE:  uiResize();
		break; case KeyFocusIn:  windowUnmark();
		break; case KeyFocusOut: windowMark();

		break; case KeyMetaEnter: error = editInsert(edit, L'\n');
		break; case KeyMetaEqual: windowToggleMute();
		break; case KeyMetaMinus: windowToggleThresh(-1);
		break; case KeyMetaPlus:  windowToggleThresh(+1);
		break; case KeyMetaSlash: windowSwap();

		break; case KeyMetaGt: windowScroll(ScrollAll, -1);
		break; case KeyMetaLt: windowScroll(ScrollAll, +1);

		break; case KeyMeta0 ... KeyMeta9: windowShow(code - KeyMeta0);
		break; case KeyMetaA: windowAuto();
		break; case KeyMetaB: error = editFn(edit, EditPrevWord);
		break; case KeyMetaD: error = editFn(edit, EditDeleteNextWord);
		break; case KeyMetaF: error = editFn(edit, EditNextWord);
		break; case KeyMetaL: windowBare();
		break; case KeyMetaM: uiWrite(windowID(), Warm, NULL, "");
		break; case KeyMetaN: windowScroll(ScrollHot, +1);
		break; case KeyMetaP: windowScroll(ScrollHot, -1);
		break; case KeyMetaQ: error = editFn(edit, EditCollapse);
		break; case KeyMetaS: uiSpoilerReveal ^= true; windowUpdate();
		break; case KeyMetaT: windowToggleTime();
		break; case KeyMetaU: windowScroll(ScrollUnread, 0);
		break; case KeyMetaV: windowScroll(ScrollPage, +1);

		break; case KeyCtrlLeft: error = editFn(edit, EditPrevWord);
		break; case KeyCtrlRight: error = editFn(edit, EditNextWord);

		break; case KEY_BACKSPACE: error = editFn(edit, EditDeletePrev);
		break; case KEY_DC: error = editFn(edit, EditDeleteNext);
		break; case KEY_DOWN: windowScroll(ScrollOne, -1);
		break; case KEY_END: error = editFn(edit, EditTail);
		break; case KEY_ENTER: inputEnter();
		break; case KEY_HOME: error = editFn(edit, EditHead);
		break; case KEY_LEFT: error = editFn(edit, EditPrev);
		break; case KEY_NPAGE: windowScroll(ScrollPage, -1);
		break; case KEY_PPAGE: windowScroll(ScrollPage, +1);
		break; case KEY_RIGHT: error = editFn(edit, EditNext);
		break; case KEY_SEND: windowScroll(ScrollAll, -1);
		break; case KEY_SHOME: windowScroll(ScrollAll, +1);
		break; case KEY_UP: windowScroll(ScrollOne, +1);
	}
	if (error) err(EX_OSERR, "editFn");
}

static void keyCtrl(wchar_t ch) {
	int error = 0;
	struct Edit *edit = &edits[windowID()];
	switch (ch ^ L'@') {
		break; case L'?': error = editFn(edit, EditDeletePrev);
		break; case L'A': error = editFn(edit, EditHead);
		break; case L'B': error = editFn(edit, EditPrev);
		break; case L'C': raise(SIGINT);
		break; case L'D': error = editFn(edit, EditDeleteNext);
		break; case L'E': error = editFn(edit, EditTail);
		break; case L'F': error = editFn(edit, EditNext);
		break; case L'H': error = editFn(edit, EditDeletePrev);
		break; case L'I': error = tabComplete(edit, windowID());
		break; case L'J': inputEnter();
		break; case L'K': error = editFn(edit, EditDeleteTail);
		break; case L'L': clearok(curscr, true);
		break; case L'N': windowShow(windowNum() + 1);
		break; case L'P': windowShow(windowNum() - 1);
		break; case L'R': windowSearch(editString(edit, &buf, &cap, NULL), -1);
		break; case L'S': windowSearch(editString(edit, &buf, &cap, NULL), +1);
		break; case L'T': error = editFn(edit, EditTranspose);
		break; case L'U': error = editFn(edit, EditDeleteHead);
		break; case L'V': windowScroll(ScrollPage, -1);
		break; case L'W': error = editFn(edit, EditDeletePrevWord);
		break; case L'X': error = macroExpand(edit); tabAccept();
		break; case L'Y': error = editFn(edit, EditPaste);
	}
	if (error) err(EX_OSERR, "editFn");
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
	struct Edit *edit = &edits[windowID()];
	for (char *ch = buf; *ch; ++ch) {
		int error = editInsert(edit, *ch);
		if (error) err(EX_OSERR, "editInsert");
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
		bool tabbing = false;
		size_t pos = edits[tab.id].pos;
		bool spr = uiSpoilerReveal;

		if (ret == KEY_CODE_YES && ch == KeyPasteOn) {
			paste = true;
		} else if (ret == KEY_CODE_YES && ch == KeyPasteOff) {
			paste = false;
		} else if (ret == KEY_CODE_YES && ch == KeyPasteManual) {
			paste ^= true;
		} else if (paste || literal) {
			int error = editInsert(&edits[windowID()], ch);
			if (error) err(EX_OSERR, "editInsert");
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
			tabbing = (ch == (L'I' ^ L'@'));
			keyCtrl(ch);
		} else {
			int error = editInsert(&edits[windowID()], ch);
			if (error) err(EX_OSERR, "editInsert");
		}
		style = false;
		literal = false;

		if (!tabbing) {
			if (edits[tab.id].pos > pos) {
				tabAccept();
			} else if (edits[tab.id].pos < pos) {
				tabReject();
			}
		}

		if (spr) {
			uiSpoilerReveal = false;
			windowUpdate();
		}
	}
	inputUpdate();
}

static int writeString(FILE *file, const char *str) {
	return (fwrite(str, strlen(str) + 1, 1, file) ? 0 : -1);
}

int inputSave(FILE *file) {
	int error;
	for (uint id = 0; id < IDCap; ++id) {
		if (!edits[id].len) continue;
		char *ptr = editString(&edits[id], &buf, &cap, NULL);
		if (!ptr) return -1;
		error = 0
			|| writeString(file, idNames[id])
			|| writeString(file, ptr);
		if (error) return error;
	}
	return writeString(file, "");
}

static ssize_t readString(FILE *file, char **buf, size_t *cap) {
	ssize_t len = getdelim(buf, cap, '\0', file);
	if (len < 0 && !feof(file)) err(EX_IOERR, "getdelim");
	return len;
}

void inputLoad(FILE *file, size_t version) {
	if (version < 8) return;
	while (0 < readString(file, &buf, &cap) && buf[0]) {
		uint id = idFor(buf);
		readString(file, &buf, &cap);
		size_t max = strlen(buf);
		int error = editReserve(&edits[id], 0, max);
		if (error) err(EX_OSERR, "editReserve");
		size_t len = mbstowcs(edits[id].buf, buf, max);
		assert(len != (size_t)-1);
		edits[id].len = len;
		edits[id].pos = len;
	}
}
