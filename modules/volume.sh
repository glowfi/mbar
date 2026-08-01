#!/bin/sh
dwm_pulse() {
	VOL=$(pamixer --get-volume)
	STATE=$(pamixer --get-mute)
	MVOL1=$(amixer -D pulse sget Capture 2>/dev/null | grep 'Left:' | awk -F'[][]' '{ print $2 }' | xargs)
	MVOL2=$(amixer -D pulse sget Capture 2>/dev/null | grep 'Mono:' | awk -F'[][]' '{ print $2 }' | xargs)
	if [ -n "$MVOL1" ]; then
		MVOL="$MVOL1"
	elif [ -n "$MVOL2" ]; then
		MVOL="$MVOL2"
	fi
	MSTATE=$(amixer -D pulse get Capture 2>/dev/null | sed 5q | tail -1 | awk '{print $NF}' | xargs)
	printf "%s" "$SEP1"

	# output volume
	if [ "$STATE" = "true" ] || [ "$VOL" -eq 0 ]; then
		printf "🔇"
	elif [ "$VOL" -le 33 ]; then
		printf "🔈 %s%%" "$VOL"
	elif [ "$VOL" -le 66 ]; then
		printf "🔉 %s%%" "$VOL"
	else
		printf "🔊 %s%%" "$VOL"
	fi

	# mic
	if [ "$MSTATE" = "[off]" ] || [ -z "$MVOL" ]; then
		printf "  🎤🔇"
	else
		printf "  🎤 %s" "$MVOL"
	fi
	printf "%s\n" "$SEP2"
}

dwm_pulse
