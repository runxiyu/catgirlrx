PREFIX ?= /usr/local
BINDIR ?= ${PREFIX}/bin
MANDIR ?= ${PREFIX}/man

CEXTS = gnu-case-range gnu-conditional-omitted-operand
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic ${CEXTS:%=-Wno-%}
LDADD.libtls = -ltls
LDADD.ncursesw = -lncursesw

-include config.mk

LDLIBS = ${LDADD.libtls} ${LDADD.ncursesw}

OBJS += buffer.o
OBJS += chat.o
OBJS += command.o
OBJS += complete.o
OBJS += config.o
OBJS += edit.o
OBJS += filter.o
OBJS += handle.o
OBJS += irc.o
OBJS += log.o
OBJS += ui.o
OBJS += url.o
OBJS += xdg.o

dev: tags all

all: catgirl

catgirl: ${OBJS}
	${CC} ${LDFLAGS} ${OBJS} ${LDLIBS} -o $@

${OBJS}: chat.h

tags: *.[ch]
	ctags -w *.[ch]

clean:
	rm -f catgirl ${OBJS} tags

install: catgirl catgirl.1
	install -d ${DESTDIR}${BINDIR} ${DESTDIR}${MANDIR}/man1
	install catgirl ${DESTDIR}${BINDIR}
	install -m 644 catgirl.1 ${DESTDIR}${MANDIR}/man1

uninstall:
	rm -f ${DESTDIR}${BINDIR}/catgirl ${DESTDIR}${MANDIR}/man1/catgirl.1

scripts/sandman: scripts/sandman.o
	${CC} ${LDFLAGS} scripts/sandman.o -framework Cocoa -o $@

install-sandman: scripts/sandman scripts/sandman.1
	install -d ${DESTDIR}${BINDIR} ${DESTDIR}${MANDIR}/man1
	install scripts/sandman ${DESTDIR}${BINDIR}
	install -m 644 scripts/sandman.1 ${DESTDIR}${MANDIR}/man1

uninstall-sandman:
	rm -f ${DESTDIR}${BINDIR}/sandman ${DESTDIR}${MANDIR}/man1/sandman.1

CHROOT_USER = chat
CHROOT_GROUP = ${CHROOT_USER}

chroot.tar: catgirl catgirl.1 scripts/chroot-prompt.sh scripts/chroot-man.sh
chroot.tar: scripts/build-chroot.sh
	sh scripts/build-chroot.sh ${CHROOT_USER} ${CHROOT_GROUP}

install-chroot: chroot.tar
	tar -px -f chroot.tar -C /home/${CHROOT_USER}

clean-chroot:
	rm -fr chroot.tar root
