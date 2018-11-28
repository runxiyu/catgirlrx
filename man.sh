#!/bin/sh
exec mandoc /usr/share/man/man1/catgirl.1 | LESSSECURE=1 less
