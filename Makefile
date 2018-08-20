LIBRESSL_PREFIX = /usr/local /usr/local/opt/libressl
PREFIX = ~/.local
CHROOT_USER = chat
CHROOT_GROUP = $(CHROOT_USER)

CFLAGS += -Wall -Wextra -Wpedantic
CFLAGS += $(LIBRESSL_PREFIX:%=-I%/include)
LDFLAGS += $(LIBRESSL_PREFIX:%=-L%/lib)
LDLIBS = -lcursesw -ltls

OBJS += chat.o
OBJS += edit.o
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

all: tags chatte

chatte: $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) $(LDLIBS) -o $@

$(OBJS): chat.h

tags: *.h *.c
	ctags -w *.h *.c

chroot.tar: chatte
	mkdir -p root
	install -d -o root -g wheel \
		root/bin \
		root/etc \
		root/home \
		root/lib \
		root/libexec \
		root/usr \
		root/usr/local \
		root/usr/local/etc \
		root/usr/local/etc/ssl \
		root/usr/share \
		root/usr/share/misc
	install -d -o $(CHROOT_USER) -g $(CHROOT_GROUP) root/home/$(CHROOT_USER)
	cp -p -f /libexec/ld-elf.so.1 root/libexec
	cp -p -f \
		/lib/libc.so.7 \
	    /lib/libedit.so.7 \
		/lib/libncursesw.so.8 \
		/lib/libthr.so.3 \
		/usr/local/lib/libcrypto.so.43 \
		/usr/local/lib/libssl.so.45 \
		/usr/local/lib/libtls.so.17 \
		root/lib
	cp -p -f /etc/hosts /etc/resolv.conf root/etc
	cp -p -f /usr/local/etc/ssl/cert.pem root/usr/local/etc/ssl
	cp -a -f /usr/share/locale root/usr/share
	cp -p -f /usr/share/misc/termcap.db root/usr/share/misc
	cp -p -f /bin/sh root/bin
	install -o root -g wheel -m 555 chatte root/bin
	tar -c -f chroot.tar -C root bin etc home lib libexec usr

clean:
	rm -f tags chatte $(OBJS) chroot.tar

install: chatte
	install chatte $(PREFIX)/bin/chatte

uninstall:
	rm -f $(PREFIX)/bin/chatte
