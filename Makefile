LIBRESSL_PREFIX = /usr/local
CFLAGS += -I${LIBRESSL_PREFIX}/include
LDFLAGS += -L${LIBRESSL_PREFIX}/lib

CFLAGS += -std=c11 -Wall -Wextra -Wpedantic
LDLIBS = -lcrypto -ltls

OBJS += chat.o
OBJS += handle.o
OBJS += irc.o
OBJS += term.o

catgirl: ${OBJS}
	${CC} ${LDFLAGS} ${OBJS} ${LDLIBS} -o $@

${OBJS}: chat.h

clean:
	rm -f catgirl ${OBJS}
