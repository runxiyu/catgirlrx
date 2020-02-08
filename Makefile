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

dev: tags all

all: catgirl

catgirl: ${OBJS}
	${CC} ${LDFLAGS} ${OBJS} ${LDLIBS} -o $@

${OBJS}: chat.h

tags: *.h *.c
	ctags -w *.h *.c

clean:
	rm -f tags catgirl ${OBJS}
