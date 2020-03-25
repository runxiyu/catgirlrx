PREFIX = /usr/local
MANDIR = ${PREFIX}/share/man

CFLAGS += -std=c11 -Wall -Wextra -Wpedantic
LDLIBS = -lcrypto -ltls -lncursesw

-include config.mk

OBJS += chat.o
OBJS += command.o
OBJS += complete.o
OBJS += config.o
OBJS += edit.o
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

tags: *.h *.c
	ctags -w *.h *.c

clean:
	rm -f tags catgirl ${OBJS}

install: catgirl catgirl.1
	install -d ${PREFIX}/bin ${MANDIR}/man1
	install catgirl ${PREFIX}/bin
	gzip -c catgirl.1 > ${MANDIR}/man1/catgirl.1.gz

uninstall:
	rm -f ${PREFIX}/bin/catgirl ${MANDIR}/man1/catgirl.1.gz

scripts/sandman: scripts/sandman.o
	${CC} ${LDFLAGS} scripts/sandman.o -framework Cocoa -o $@

install-sandman: scripts/sandman scripts/sandman.1
	install -d ${PREFIX}/bin ${MANDIR}/man1
	install scripts/sandman ${PREFIX}/bin
	gzip -c scripts/sandman.1 > ${MANDIR}/man1/sandman.1.gz

uninstall-sandman:
	rm -f ${PREFIX}/bin/sandman ${MANDIR}/man1/sandman.1.gz

CHROOT_USER = chat
CHROOT_GROUP = ${CHROOT_USER}

chroot.tar: catgirl catgirl.1 scripts/chroot-prompt.sh scripts/chroot-man.sh
	install -d -o root -g wheel \
		root \
		root/bin \
		root/etc/ssl \
		root/home \
		root/lib \
		root/libexec \
		root/usr/bin \
		root/usr/share/man \
		root/usr/share/misc
	install -d -o ${CHROOT_USER} -g ${CHROOT_GROUP} \
		root/home/${CHROOT_USER} \
		root/home/${CHROOT_USER}/.local/share
	cp -fp /libexec/ld-elf.so.1 root/libexec
	cp -fp \
		/lib/libc.so.7 \
		/lib/libncursesw.so.8 \
		/lib/libthr.so.3 \
		/lib/libz.so.6 \
		/usr/local/lib/libcrypto.so.45 \
		/usr/local/lib/libssl.so.47 \
		/usr/local/lib/libtls.so.19 \
		root/lib
	chflags noschg root/libexec/* root/lib/*
	cp -fp /etc/hosts /etc/resolv.conf root/etc
	cp -fp /etc/ssl/cert.pem root/etc/ssl
	cp -af /usr/share/locale root/usr/share
	cp -fp /usr/share/misc/termcap.db root/usr/share/misc
	cp -fp /rescue/sh /usr/bin/mandoc /usr/bin/less root/bin
	${MAKE} install PREFIX=root/usr
	install scripts/chroot-prompt.sh root/usr/bin/catgirl-prompt
	install scripts/chroot-man.sh root/usr/bin/man
	tar -c -f chroot.tar -C root bin etc home lib libexec usr

install-chroot: chroot.tar
	tar -x -f chroot.tar -C /home/${CHROOT_USER}

clean-chroot:
	rm -fr chroot.tar root
