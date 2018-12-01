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

#import <Cocoa/Cocoa.h>
#import <err.h>
#import <signal.h>
#import <stdlib.h>
#import <sysexits.h>
#import <unistd.h>

void handler(int sig) {
	(void)sig;
	int status;
	pid_t pid = wait(&status);
	if (pid < 0) _exit(EX_OSERR);
	if (WIFSIGNALED(status)) {
		_exit(128 + WTERMSIG(status));
	} else {
		_exit(WEXITSTATUS(status));
	}
}

int main(int argc, char *argv[]) {
	if (argc < 2) return EX_USAGE;

	sigset_t mask;
	sigemptyset(&mask);
	struct sigaction sa = {
		.sa_handler = handler,
		.sa_mask = mask,
		.sa_flags = SA_RESTART,
	};
	sigaction(SIGCHLD, &sa, NULL);

	pid_t pid = fork();
	if (pid < 0) err(EX_OSERR, "fork");
	if (!pid) {
		execvp(argv[1], &argv[1]);
		err(EX_NOINPUT, "%s", argv[1]);
	}

	[
		[[NSWorkspace sharedWorkspace] notificationCenter]
		addObserverForName: NSWorkspaceWillSleepNotification
		object: nil
		queue: [NSOperationQueue mainQueue]
		usingBlock: ^(NSNotification *note) {
			(void)note;
			int error = kill(pid, SIGTSTP);
			if (error) err(EX_UNAVAILABLE, "kill %d", pid);
		}
	];
	
	[
		[[NSWorkspace sharedWorkspace] notificationCenter]
		addObserverForName: NSWorkspaceDidWakeNotification
		object: nil
		queue: [NSOperationQueue mainQueue]
		usingBlock: ^(NSNotification *note) {
			(void)note;
			int error = kill(pid, SIGCONT);
			if (error) err(EX_UNAVAILABLE, "kill %d", pid);
		}
	];

	[[NSApplication sharedApplication] run];
}
