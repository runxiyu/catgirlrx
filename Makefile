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
