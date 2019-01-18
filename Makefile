PREFIX = ~/.local
MANDIR = $(PREFIX)/share/man/man
CHROOT_USER = chat
CHROOT_GROUP = $(CHROOT_USER)
LIBRESSL_PREFIX = /usr/local

CFLAGS += -std=c11 -Wall -Wextra -Wpedantic
CFLAGS += -I$(LIBRESSL_PREFIX)/include
LDFLAGS += -L$(LIBRESSL_PREFIX)/lib
LDLIBS = -lcursesw -ltls

BINS = catgirl
MANS = catgirl.1

-include config.mk

OBJS += chat.o
OBJS += edit.o
OBJS += event.o
OBJS += format.o
OBJS += handle.o
OBJS += input.o
OBJS += irc.o
OBJS += log.o
OBJS += pls.o
OBJS += tab.o
OBJS += tag.o
OBJS += term.o
OBJS += ui.o
OBJS += url.o

TESTS += format.t
TESTS += pls.t
TESTS += term.t

all: tags $(BINS) test

$(OBJS): chat.h

catgirl: $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) $(LDLIBS) -o $@

.SUFFIXES: .t

.c.t:
	$(CC) $(CFLAGS) -DTEST $(LDFLAGS) $< $(LDLIBS) -o $@

test: $(TESTS)
	set -e; $(TESTS:%=./%;)

tags: *.h *.c
	ctags -w *.h *.c

install: $(BINS) $(MANS)
	install -d $(PREFIX)/bin $(MANDIR)1
	install $(BINS) $(PREFIX)/bin
	install -m 644 $(MANS) $(MANDIR)1

uninstall:
	rm -f $(BINS:%=$(PREFIX)/bin/%)
	rm -f $(MAN:%=%(MANDIR)1/%)

chroot.tar: catgirl catgirl.1 man.sh
	install -d -o root -g wheel \
		root \
		root/bin \
		root/etc \
		root/home \
		root/lib \
		root/libexec \
		root/usr/bin \
		root/usr/local/etc/ssl \
		root/usr/share/man \
		root/usr/share/misc
	install -d -o $(CHROOT_USER) -g $(CHROOT_GROUP) root/home/$(CHROOT_USER)
	cp -fp /libexec/ld-elf.so.1 root/libexec
	cp -fp \
		/lib/libc.so.7 \
		/lib/libncursesw.so.8 \
		/lib/libthr.so.3 \
		/lib/libz.so.6 \
		/usr/local/lib/libcrypto.so.44 \
		/usr/local/lib/libssl.so.46 \
		/usr/local/lib/libtls.so.18 \
		root/lib
	cp -fp /etc/hosts /etc/resolv.conf root/etc
	cp -fp /usr/local/etc/ssl/cert.pem root/usr/local/etc/ssl
	cp -af /usr/share/locale root/usr/share
	cp -fp /usr/share/misc/termcap.db root/usr/share/misc
	cp -fp /rescue/sh /usr/bin/mandoc /usr/bin/less root/bin
	$(MAKE) install PREFIX=root/usr
	install man.sh root/usr/bin/man
	tar -cf chroot.tar -C root bin etc home lib libexec usr

install-chroot: chroot.tar
	tar -xf chroot.tar -C /home/$(CHROOT_USER)

clean:
	rm -fr $(OBJS) $(BINS) $(TESTS) tags root chroot.tar

README: catgirl.7
	mandoc catgirl.7 | col -bx > README
