#!/bin/sh

pgrep -x wlsunset >/dev/null && echo "🌙 night" || echo "☀️ day"
