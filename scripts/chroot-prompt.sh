#!/bin/sh
set -eu

printf 'Name: '
read -r nick rest
printf '%s %s\n' "$nick" "${SSH_CLIENT:-}" >>nicks.log
exec catgirl -n "$nick" -s "$nick" "$@"
