/* Copyright (C) 2022  June McEnroe <june@causal.agency>
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
 * If you modify this Program, or any covered work, by linking or
 * combining it with OpenSSL (or a modified version of that library),
 * containing parts covered by the terms of the OpenSSL License and the
 * original SSLeay license, the licensors of this Program grant you
 * additional permission to convey the resulting work. Corresponding
 * Source for a non-source form of such a combination shall include the
 * source code for the parts of OpenSSL used as well as that of the
 * covered work.
 */

#include <stdbool.h>
#include <stddef.h>

enum EditMode {
	EditEmacs,
};

struct Edit {
	enum EditMode mode;
	wchar_t *buf;
	size_t pos;
	size_t len;
	size_t cap;
	struct {
		wchar_t *buf;
		size_t len;
	} cut;
	struct {
		char *buf;
		size_t pos;
		size_t len;
	} mbs;
};

enum EditFn {
	EditHead,
	EditTail,
	EditPrev,
	EditNext,
	EditPrevWord,
	EditNextWord,
	EditDeleteHead,
	EditDeleteTail,
	EditDeletePrev,
	EditDeleteNext,
	EditDeletePrevWord,
	EditDeleteNextWord,
	EditPaste,
	EditTranspose,
	EditCollapse,
	EditClear,
};

// Perform an editing function.
int editFn(struct Edit *e, enum EditFn fn);

// Perform a vi-mode editing function.
int editVi(struct Edit *e, wchar_t ch);

// Insert a character at the cursor.
int editInsert(struct Edit *e, wchar_t ch);

// Convert the buffer to a multi-byte string stored in e->mbs.
char *editString(struct Edit *e);

// Free all buffers.
void editFree(struct Edit *e);

// Reserve a range in the buffer.
int editReserve(struct Edit *e, size_t index, size_t count);

// Copy a range of the buffer into e->cut.
int editCopy(struct Edit *e, size_t index, size_t count);

// Delete a range from the buffer. If cut is true, copy the deleted portion.
int editDelete(struct Edit *e, bool cut, size_t index, size_t count);
