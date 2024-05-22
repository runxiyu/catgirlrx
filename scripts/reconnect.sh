#!/bin/sh
set -u

while :; do
	catgirl "$@"
	status=$?
	if [ $status -ne 69 ]; then
		exit $status
	fi
done
