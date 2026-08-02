#!/bin/sh

vol=$(pamixer --get-volume 2>/dev/null)
case "$vol" in
'' | *[!0-9]*)
	printf '🔇 --\n'
	exit 0
	;;
esac

# speaker
if [ "$(pamixer --get-mute)" = true ] || [ "$vol" -eq 0 ]; then
	printf '🔇'
elif [ "$vol" -le 33 ]; then
	printf '🔈 %s%%' "$vol"
elif [ "$vol" -le 66 ]; then
	printf '🔉 %s%%' "$vol"
else
	printf '🔊 %s%%' "$vol"
fi

# mic
if [ "$(pamixer --default-source --get-mute 2>/dev/null)" = true ]; then
	printf '  🎤🔇\n'
else
	printf '  🎤 %s%%\n' "$(pamixer --default-source --get-volume 2>/dev/null)"
fi
