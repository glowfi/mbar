#!/bin/sh

con=$(nmcli -t -f NAME,TYPE connection show --active 2>/dev/null |
	awk -F: '/ethernet|wireless/ {print $1; exit}')

if [ -n "$con" ]; then
	printf '🌐 %s\n' "$con"
else
	printf '❌ offline\n'
fi
