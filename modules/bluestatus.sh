#!/bin/sh

bt_service_up() {
	if command -v systemctl >/dev/null 2>&1 && [ -d /run/systemd/system ]; then
		systemctl is-active --quiet bluetooth.service
	elif command -v rc-service >/dev/null 2>&1; then
		rc-service bluetooth status >/dev/null 2>&1
	else
		return 0 # unknown init: skip pre-check, timeouts below protect us
	fi
}

if ! bt_service_up; then
	echo "ᛒ Off"
	exit 0
fi

if ! timeout 1 bluetoothctl show 2>/dev/null | grep -q "Powered: yes"; then
	echo "ᛒ Off"
	exit 0
fi

count=$(timeout 1 bluetoothctl devices Connected 2>/dev/null | grep -c ^Device)

if [ "$count" -eq 1 ]; then
	name=$(timeout 1 bluetoothctl devices Connected 2>/dev/null | cut -d' ' -f3-)
	echo "ᛒ $name"
else
	echo "ᛒ On [$count]"
fi
