#!/usr/bin/env bash
# S0: stacja uruchamia 5 procesów potomnych, a po SIGINT kończy je i usuwa wszystkie obiekty IPC.
# Tak samo po SIGHUP, który system wysyła przy zamknięciu terminala.
set -euo pipefail
cd "$(dirname "$0")/../.."
source tests/lib/scenario.sh

children_running() {
    [ "$(pgrep -c -P "$station_pid" || true)" -eq 5 ]
}

start_station
# Czekamy na wpis bramy, która startuje jako ostatnia, a nie stały czas.
wait_for_log 'gateway +start' 2
wait_until 2 "oczekiwano 5 procesów potomnych stacji" children_running

stop_station
log_has 'station +koniec' || fail "brak wpisu o zakończeniu w stacja.log"
[ "$(grep -c 'port [1-3]    koniec' stacja.log)" -eq 3 ] || fail "nie wszystkie porty zakończyły się"

start_station
wait_for_log 'gateway +start' 2
kill -HUP "$station_pid"
finish_station
log_has 'station +koniec' || fail "brak wpisu o zakończeniu po SIGHUP"
echo "$scenario: OK"
