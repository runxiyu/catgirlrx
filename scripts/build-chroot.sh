#!/bin/sh
set -eux

CHROOT_USER=$1
CHROOT_GROUP=$2

if [ "$(uname)" = 'OpenBSD' ]; then
	install -d -o root -g wheel \
		root \
		root/bin \
		root/etc/ssl \
		root/home \
		root/usr/bin \
		root/usr/lib \
		root/usr/libexec \
		root/usr/share/man
	install -d -o ${CHROOT_USER} -g ${CHROOT_GROUP} \
		root/home/${CHROOT_USER} \
		root/home/${CHROOT_USER}/.local/share

	cp -fp /bin/sh root/bin
	cp -fp /usr/libexec/ld.so root/usr/libexec
	export LD_TRACE_LOADED_OBJECTS_FMT1='%p\n'
	export LD_TRACE_LOADED_OBJECTS_FMT2=''
	for bin in ./catgirl /usr/bin/mandoc /usr/bin/less; do
		LD_TRACE_LOADED_OBJECTS=1 $bin | xargs -t -J % cp -fp % root/usr/lib
	done
	cp -fp /usr/bin/printf /usr/bin/mandoc /usr/bin/less root/usr/bin
	make install DESTDIR=root PREFIX=/usr MANDIR=/usr/share/man
	install scripts/chroot-prompt.sh root/usr/bin/catgirl-prompt
	install scripts/chroot-man.sh root/usr/bin/man

	cp -fp /etc/hosts /etc/resolv.conf root/etc
	cp -fp /etc/ssl/cert.pem root/etc/ssl
	cp -af /usr/share/locale /usr/share/terminfo root/usr/share

	tar -c -f chroot.tar -C root bin etc home usr

elif [ "$(uname)" = 'FreeBSD' ]; then
	install -d -o root -g wheel \
		root \
		root/bin \
		root/etc \
		root/home \
		root/lib \
		root/libexec \
		root/usr/bin \
		root/usr/local/etc/ssl \
		root/usr/share/man \
		root/usr/share/misc
	install -d -o ${CHROOT_USER} -g ${CHROOT_GROUP} \
		root/home/${CHROOT_USER} \
		root/home/${CHROOT_USER}/.local/share

	cp -fp /libexec/ld-elf.so.1 root/libexec
	ldd -f '%p\n' catgirl /usr/bin/mandoc /usr/bin/less \
		| sort -u | xargs -t -J % cp -fp % root/lib
	chflags noschg root/libexec/* root/lib/*
	cp -fp /rescue/sh /usr/bin/mandoc /usr/bin/less root/bin
	make install DESTDIR=root PREFIX=/usr MANDIR=/usr/share/man
	install scripts/chroot-prompt.sh root/usr/bin/catgirl-prompt
	install scripts/chroot-man.sh root/usr/bin/man

	cp -fp /etc/hosts /etc/resolv.conf root/etc
	cp -fp /usr/local/etc/ssl/cert.pem root/usr/local/etc/ssl
	cp -af /usr/share/locale root/usr/share
	cp -fp /usr/share/misc/termcap.db root/usr/share/misc

	tar -c -f chroot.tar -C root bin etc home lib libexec usr

else
	echo "Don't know how to build chroot on $(uname)" >&2
	exit 1
fi
