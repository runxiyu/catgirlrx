PREFIX = ~/.local
MANPATH = $(PREFIX)/share/man
LIBRESSL_PREFIX = /usr/local /usr/local/opt/libressl
CHROOT_USER = chat
CHROOT_GROUP = $(CHROOT_USER)

CFLAGS += -Wall -Wextra -Wpedantic
CFLAGS += $(LIBRESSL_PREFIX:%=-I%/include)
LDFLAGS += $(LIBRESSL_PREFIX:%=-L%/lib)
LDLIBS = -lcursesw -ltls

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
TESTS += term.t

all: tags chatte

tags: *.h *.c
	ctags -w *.h *.c

chatte: $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) $(LDLIBS) -o $@

$(OBJS): chat.h

test: $(TESTS)
	$(TESTS:%=./%;)

.SUFFIXES: .t

.c.t:
	$(CC) $(CFLAGS) -DTEST $(LDFLAGS) $< $(LDLIBS) -o $@

install: chatte chatte.1
	install -d $(PREFIX)/bin $(MANPATH)/man1
	install chatte $(PREFIX)/bin/chatte
	install -m 644 chatte.1 $(MANPATH)/man1/chatte.1

uninstall:
	rm -f $(PREFIX)/bin/chatte
	rm -f $(MANPATH)/man1/chatte.1

chroot.tar: chatte chatte.1 man.sh
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
	cp -p -f /libexec/ld-elf.so.1 root/libexec
	cp -p -f \
		/lib/libc.so.7 \
	    /lib/libedit.so.7 \
		/lib/libncursesw.so.8 \
		/lib/libthr.so.3 \
		/lib/libz.so.6 \
		/usr/local/lib/libcrypto.so.43 \
		/usr/local/lib/libssl.so.45 \
		/usr/local/lib/libtls.so.17 \
		root/lib
	cp -p -f /etc/hosts /etc/resolv.conf root/etc
	cp -p -f /usr/local/etc/ssl/cert.pem root/usr/local/etc/ssl
	cp -a -f /usr/share/locale root/usr/share
	cp -p -f /usr/share/misc/termcap.db root/usr/share/misc
	cp -p -f /bin/sh /usr/bin/mandoc /usr/bin/less root/bin
	$(MAKE) install PREFIX=root/usr
	install man.sh root/usr/bin/man
	tar -c -f chroot.tar -C root bin etc home lib libexec usr

clean:
	rm -rf tags chatte $(OBJS) $(TESTS) root chroot.tar
