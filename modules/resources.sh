#!/bin/sh

# memory
mem=$(free -h --si | awk '/^Mem/ {printf "%s/%s", $3, $2}')

# cpu usage since last call (cached, like traffic)
cache=${XDG_CACHE_HOME:-$HOME/.cache}/cpu_last
read -r _ u n s idle rest </proc/stat
total=$((u + n + s + idle))
[ -f "$cache" ] && read -r ot oi <"$cache" || {
	ot=0
	oi=0
}
printf '%d %d\n' "$total" "$idle" >"$cache"
dt=$((total - ot))
di=$((idle - oi))
[ "$dt" -gt 0 ] && cpu=$(((dt - di) * 100 / dt)) || cpu=0

# storage
sto=$(df -h /home | awk 'NR==2 {printf "%s/%s:%s", $3, $2, $5}')

printf '🧠 %s  🖥 %s%%  💾 %s\n' "$mem" "$cpu" "$sto"
