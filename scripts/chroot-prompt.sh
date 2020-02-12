#!/bin/sh
set -eu

printf 'Name: '
read -r nick rest
exec catgirl -n "$nick" -s "$nick" "$@"
