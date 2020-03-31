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

#import <Cocoa/Cocoa.h>
#import <err.h>
#import <signal.h>
#import <stdio.h>
#import <stdlib.h>
#import <sysexits.h>
#import <unistd.h>

typedef unsigned uint;

static pid_t pid;
static void spawn(char *argv[]) {
	pid = fork();
	if (pid < 0) err(EX_OSERR, "fork");
	if (pid) return;
	execvp(argv[0], argv);
	err(EX_CONFIG, "%s", argv[0]);
}

static void handler(int signal) {
	(void)signal;
	int status;
	pid_t pid = wait(&status);
	if (pid < 0) _exit(EX_OSERR);
	_exit(status);
}

int main(int argc, char *argv[]) {
	uint delay = 8;

	for (int opt; 0 < (opt = getopt(argc, argv, "t:"));) {
		switch (opt) {
			break; case 't': delay = strtoul(optarg, NULL, 10);
			break; default:  return EX_USAGE;
		}
	}
	argc -= optind;
	argv += optind;
	if (!argc) errx(EX_USAGE, "command required");

	NSWorkspace *workspace = [NSWorkspace sharedWorkspace];
	NSNotificationCenter *notifCenter = [workspace notificationCenter];

	[notifCenter addObserverForName:NSWorkspaceWillSleepNotification
							 object:nil
							  queue:nil
						 usingBlock:^(NSNotification *notif) {
							 (void)notif;
							 signal(SIGCHLD, SIG_IGN);
							 int error = kill(pid, SIGHUP);
							 if (error) err(EX_UNAVAILABLE, "kill");
							 int status;
							 wait(&status);
						 }];

	[notifCenter addObserverForName:NSWorkspaceDidWakeNotification
							 object:nil
							  queue:nil
						 usingBlock:^(NSNotification *notif) {
							 (void)notif;
							 warnx("waiting %u seconds...", delay);
							 sleep(delay);
							 signal(SIGCHLD, handler);
							 spawn(argv);
						 }];

	signal(SIGCHLD, handler);
	spawn(argv);

	[[NSApplication sharedApplication] run];
}
