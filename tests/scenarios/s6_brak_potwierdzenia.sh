#!/usr/bin/env bash
# S6: statek nie potwierdza przydziału - po ACK_TIMEOUT_MS sterownik anuluje rezerwację,
# port wraca do stanu WOLNY, a proces statku zostaje zakończony.
set -euo pipefail
cd "$(dirname "$0")/../.."
source tests/lib/scenario.sh

ship_queue_gone() {
    [ ! -e "/dev/mqueue/ss_ship_$1" ]
}

start_station
launch_ship wahadlowiec --no-ack
pid=$ship_pid
wait_for_log "przydzielony statkowi $pid\$" 2
port=$(grep -E "przydzielony statkowi $pid\$" stacja.log | awk '{print $4}')

wait_for_log "statek $pid nie potwierdził przydziału portu $port" 7
wait_for_log "port $port +port: ZAREZERWOWANY -> WOLNY" 2
wait_until 2 "statek $pid nie zakończył się" ship_queue_gone "$pid"

stop_station
echo "$scenario: OK"
