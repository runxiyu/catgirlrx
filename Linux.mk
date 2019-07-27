LIBRESSL_PREFIX = /usr/local
CFLAGS += -D_GNU_SOURCE
LDLIBS = -lncursesw -lpthread
LDLIBS += ${LIBRESSL_PREFIX}/lib/libtls.a
LDLIBS += ${LIBRESSL_PREFIX}/lib/libssl.a
LDLIBS += ${LIBRESSL_PREFIX}/lib/libcrypto.a
