#!/usr/bin/env bash
# S4: statek zabity (kill -9) w trakcie obsługi - generator wykrywa zakończenie przez sygnał,
# sterownik zwalnia port, port przerywa obsługę i oddaje energię.
set -euo pipefail
cd "$(dirname "$0")/../.."
source tests/lib/scenario.sh

start_station
# Statek generatora (tylko jego dzieci są zgłaszane jako utracone) w trakcie obsługi.
wait_for_log 'port [1-3] +obsługa: [A-Z_]+ -> TRWA' 20
port=$(grep -E 'port [1-3] +obsługa: [A-Z_]+ -> TRWA' stacja.log | head -1 | awk '{print $3}')
pid=$(grep -E "port $port przydzielony statkowi" stacja.log | tail -1 | awk '{print $NF}')

kill -9 "$pid"
wait_for_log "generator +statek $pid zabity sygnałem 9" 2
wait_for_log "statek $pid utracony" 2
wait_for_log "port $port +obsługa: TRWA -> BRAK" 2
wait_for_log "port $port +port: ODDOKOWANIE -> WOLNY" 4
[ ! -e "/dev/mqueue/ss_ship_$pid" ] || fail "kolejka utraconego statku nie została usunięta"

stop_station
echo "$scenario: OK"
