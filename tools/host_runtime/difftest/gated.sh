#!/bin/sh
# gated.sh CMD...: runs CMD once the machine's 1-minute load is at most 90 (the helpers' load gate)
while :; do
    load=$(sysctl -n vm.loadavg | awk '{print int($2)}')
    [ "$load" -le 90 ] && break
    sleep 30
done
exec "$@"
