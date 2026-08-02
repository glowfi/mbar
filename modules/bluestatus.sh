#!/bin/sh

timeout 1 bluetoothctl show 2>/dev/null | grep -q "Powered: yes" || {
	echo " Off"
	exit 0
}

devices=$(timeout 1 bluetoothctl devices Connected 2>/dev/null)
count=$(printf '%s\n' "$devices" | grep -c '^Device')

case "$count" in
0) echo " On" ;;
1) echo " $(printf '%s\n' "$devices" | cut -d' ' -f3-)" ;;
*) echo " On [$count]" ;;
esac
