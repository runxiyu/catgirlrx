LIBRESSL_PREFIX = /usr/pkg/libressl
LDFLAGS += -rpath=$(LIBRESSL_PREFIX)/lib
LDLIBS = -lcurses -ltls
