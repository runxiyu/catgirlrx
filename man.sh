#!/bin/sh
exec mandoc /usr/share/man/man1/chatte.1 | LESSSECURE=1 less
