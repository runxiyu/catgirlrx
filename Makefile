USER = chat
CFLAGS += -Wall -Wextra -Wpedantic
CFLAGS += -I/usr/local/include
LDFLAGS += -L/usr/local/lib
LDLIBS = -lcursesw -ltls
OBJS = chat.o client.o handle.o input.o ui.o

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
	install -o root -g wheel -m 555 /libexec/ld-elf.so.1 root/libexec
	install -o root -g wheel -m 444 \
		/lib/libc.so.7 \
		/lib/libncursesw.so.8 \
		/lib/libthr.so.3 \
		/usr/local/lib/libcrypto.so.43 \
		/usr/local/lib/libssl.so.45 \
		/usr/local/lib/libtls.so.17 \
		root/lib
	install -o root -g wheel -m 444 /etc/hosts /etc/resolv.conf root/etc
	install -o root -g wheel -m 444 /usr/local/etc/ssl/cert.pem root/usr/local/etc/ssl
	install -o root -g wheel -m 444 /usr/share/misc/termcap.db root/usr/share/misc
	install -o root -g wheel -m 555 /bin/sh root/bin
	install -o root -g wheel -m 555 chat root/bin
	tar -c -f chroot.tar -C root bin etc home lib libexec usr

clean:
	rm -f tags chat $(OBJS) chroot.tar
