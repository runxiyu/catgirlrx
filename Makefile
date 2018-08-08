USER = chat
CFLAGS += -Wall -Wextra -Wpedantic
CFLAGS += -I/usr/local/include -I/usr/local/opt/libressl/include
LDFLAGS += -L/usr/local/lib -L/usr/local/opt/libressl/lib
LDLIBS = -lcursesw -ltls
OBJS = chat.o edit.o handle.o input.o irc.o pls.o tab.o ui.o

all: tags chat

chat: $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) $(LDLIBS) -o $@

$(OBJS): chat.h

tags: *.h *.c
	ctags -w *.h *.c

chroot.tar: chat
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
	install -d -o $(USER) -g $(USER) root/home/$(USER)
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
	install -o root -g wheel -m 555 chat root/bin
	tar -c -f chroot.tar -C root bin etc home lib libexec usr

clean:
	rm -f tags chat $(OBJS) chroot.tar
