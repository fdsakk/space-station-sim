#!/usr/bin/env bash
# S9: stacja zatrzymana tuż po fali zgłoszeń - generator wysyła SIGTERM do statków, które dopiero
# co powstały. Żaden nie może zgubić tego sygnału, bo stacja czekałaby na niego bez końca.
set -euo pipefail
cd "$(dirname "$0")/../.."
source tests/lib/scenario.sh

for _ in {1..10}; do
    start_station
    wait_for_log 'gateway +start' 2
    ./build/panel fala
    kill -INT "$station_pid"
    wait_for_log 'station +koniec' 5
    finish_station
done
echo "$scenario: OK"
