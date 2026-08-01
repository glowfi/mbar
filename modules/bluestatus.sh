#!/bin/sh

if ! bluetoothctl show 2>/dev/null | grep -q "Powered: yes"; then
	echo "ᛒ Off"
	exit 0
fi

count=$(bluetoothctl devices Connected 2>/dev/null | grep -c ^Device)

if [ "$count" -eq 1 ]; then
	name=$(bluetoothctl devices Connected | cut -d' ' -f3-)
	echo "ᛒ $name"
else
	echo "ᛒ On [$count]"
fi
