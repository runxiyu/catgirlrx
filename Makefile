CFLAGS += -Wall -Wextra -Wpedantic
CFLAGS += -I/usr/local/include
LDFLAGS += -L/usr/local/lib
LDLIBS = -lcurses -ltls

all: tags chat

tags: *.c
	ctags -w *.c

clean:
	rm -f tags chat
