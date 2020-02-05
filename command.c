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

#include <stdio.h>
#include <stdlib.h>

#include "chat.h"

void command(size_t id, char *input) {
	if (id == Debug) {
		ircFormat("%s\r\n", input);
		return;
	}
	ircFormat("PRIVMSG %s :%s\r\n", idNames[id], input);
	struct Message msg = {
		.nick = self.nick,
		.user = self.user,
		.cmd = "PRIVMSG",
		.params[0] = idNames[id],
		.params[1] = input,
	};
	handle(msg);
}
