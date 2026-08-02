#!/bin/sh

update() {
	sum=0
	for f; do
		read -r i <"$f" && sum=$((sum + i))
	done
	cache=${XDG_CACHE_HOME:-$HOME/.cache}/traffic_$1_last
	[ -f "$cache" ] && read -r old <"$cache" || old=0
	printf '%d\n' "$sum" >"$cache"
	echo $((sum - old))
}

rx=$(update rx /sys/class/net/[ew]*/statistics/rx_bytes)
tx=$(update tx /sys/class/net/[ew]*/statistics/tx_bytes)

printf '🔻%sB 🔺%sB\n' $(numfmt --to=iec "$rx" "$tx")
