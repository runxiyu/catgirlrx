PREFIX ?= /usr/local
BINDIR ?= ${PREFIX}/bin
MANDIR ?= ${PREFIX}/man

CFLAGS += -Wall -Wextra

-include ../config.mk

LDLIBS = -framework Cocoa

all: sandman

clean:
	rm -f sandman

install: sandman sandman.1
	install -d ${DESTDIR}${BINDIR} ${DESTDIR}${MANDIR}/man1
	install sandman ${DESTDIR}${BINDIR}
	install -m 644 sandman.1 ${DESTDIR}${MANDIR}/man1

uninstall:
	rm -f ${DESTDIR}${BINDIR}/sandman ${DESTDIR}/man/man1/sandman.1
