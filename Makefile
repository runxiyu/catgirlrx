LIBRESSL_PREFIX = /usr/local
CFLAGS += -I${LIBRESSL_PREFIX}/include
LDFLAGS += -L${LIBRESSL_PREFIX}/lib

CFLAGS += -std=c11 -Wall -Wextra -Wpedantic
LDLIBS = -lcurses -lcrypto -ltls

OBJS += chat.o
OBJS += handle.o
OBJS += irc.o
OBJS += ui.o

dev: tags all

all: catgirl

catgirl: ${OBJS}
	${CC} ${LDFLAGS} ${OBJS} ${LDLIBS} -o $@

${OBJS}: chat.h

tags: *.h *.c
	ctags -w *.h *.c

clean:
	rm -f tags catgirl ${OBJS}
