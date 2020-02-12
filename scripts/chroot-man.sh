#!/bin/sh
exec mandoc /usr/share/man/man1/catgirl.1.gz | LESSSECURE=1 less
