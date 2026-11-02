#!/usr/bin/env bash
# S11: stacja zabita przez kill -9 nie może sprzątać, ale jej procesy potomne i statki kończą się
# same, zamiast działać dalej bez rodzica.
set -euo pipefail
cd "$(dirname "$0")/../.."
source tests/lib/scenario.sh

no_station_processes() {
    ! pgrep -x 'port|generator|gateway|ship' >/dev/null
}

start_station
wait_for_log 'gateway +start' 2
./build/panel fala
wait_for_log 'generator +uruchomiono statek' 2

kill -9 "$station_pid"
wait "$station_pid" || true
station_pid=""
wait_until 3 "procesy stacji działają dalej po jej zabiciu" no_station_processes

make -s clean-ipc
echo "$scenario: OK"
