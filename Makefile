PREFIX ?= /usr/local
BINDIR ?= ${PREFIX}/bin
MANDIR ?= ${PREFIX}/man

CEXTS = gnu-case-range gnu-conditional-omitted-operand
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Wmissing-prototypes
CFLAGS += ${CEXTS:%=-Wno-%}
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
OBJS += input.o
OBJS += irc.o
OBJS += log.o
OBJS += ui.o
OBJS += url.o
OBJS += window.o
OBJS += xdg.o

TESTS += edit.t

dev: tags all check

all: catgirl

catgirl: ${OBJS}
	${CC} ${LDFLAGS} ${OBJS} ${LDLIBS} -o $@

${OBJS}: chat.h

edit.o edit.t input.o: edit.h

check: ${TESTS}

.SUFFIXES: .t

.c.t:
	${CC} ${CFLAGS} -DTEST ${LDFLAGS} $< ${LDLIBS} -o $@
	./$@ || rm $@

tags: *.[ch]
	ctags -w *.[ch]

clean:
	rm -f catgirl ${OBJS} ${TESTS} tags

install: catgirl catgirl.1
	install -d ${DESTDIR}${BINDIR} ${DESTDIR}${MANDIR}/man1
	install catgirl ${DESTDIR}${BINDIR}
	install -m 644 catgirl.1 ${DESTDIR}${MANDIR}/man1

uninstall:
	rm -f ${DESTDIR}${BINDIR}/catgirl ${DESTDIR}${MANDIR}/man1/catgirl.1

CHROOT_USER = chat
CHROOT_GROUP = ${CHROOT_USER}

chroot.tar: catgirl catgirl.1 scripts/chroot-prompt.sh scripts/chroot-man.sh
chroot.tar: scripts/build-chroot.sh
	sh scripts/build-chroot.sh ${CHROOT_USER} ${CHROOT_GROUP}

install-chroot: chroot.tar
	tar -px -f chroot.tar -C /home/${CHROOT_USER}

clean-chroot:
	rm -fr chroot.tar root
