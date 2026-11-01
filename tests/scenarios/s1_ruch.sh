#!/usr/bin/env bash
# S1: zwykły ruch - trzy statki obsłużone równolegle w trzech portach, generator dokłada kolejne.
set -euo pipefail
cd "$(dirname "$0")/../.."
source tests/lib/scenario.sh

start_station
first=()
for i in 1 2 3; do
    launch_ship wahadlowiec
    first+=("$ship_pid")
done

for pid in "${first[@]}"; do
    wait_for_log "statek $pid: ODDOKOWUJE -> ODLECIAL" 30
done
for port in 1 2 3; do
    log_has "port $port +port: ODDOKOWANIE -> WOLNY" || fail "port $port nie obsłużył statku"
done
log_has 'generator +uruchomiono statek' || fail "generator nie uruchomił żadnego statku"

stop_station
echo "$scenario: OK"
