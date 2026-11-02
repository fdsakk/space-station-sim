#!/usr/bin/env bash
# S10: statek znika z portu bez zgłoszenia od generatora - zwykły kill statku generatora (kończy się
# kodem 0) i kill -9 statku uruchomionego ręcznie. Sterownik sam wykrywa brak procesu i zwalnia port.
# Statek uruchomiony ręcznie kończy się też sam, gdy stacja się zatrzyma.
set -euo pipefail
cd "$(dirname "$0")/../.."
source tests/lib/scenario.sh

# Numer portu przydzielonego statkowi (z wpisu sterownika w dzienniku).
port_of() {
    grep -E "przydzielony statkowi $1\$" stacja.log | head -1 | awk '{print $4}'
}

ship_queue_gone() {
    [ ! -e "/dev/mqueue/ss_ship_$1" ]
}

ship_released() {
    local pid=$1 port
    port=$(port_of "$pid")
    wait_for_log "statek $pid zniknął bez zgłoszenia" 2
    wait_for_log "port $port +port: ZADOKOWANY -> ODDOKOWANIE" 2
    wait_for_log "port $port +port: ODDOKOWANIE -> WOLNY" 4
    [ ! -e "/dev/mqueue/ss_ship_$pid" ] || fail "kolejka statku $pid nie została usunięta"
}

start_station
# Pierwszy statek generatora: po zwykłym kill sprząta swoją kolejkę i kończy się kodem 0.
./build/panel statek wahadlowiec
wait_for_log 'generator +uruchomiono statek' 2
pid=$(grep -m1 -E 'generator +uruchomiono statek' stacja.log | grep -oE 'statek [0-9]+' | awk '{print $2}')
wait_for_log "statek $pid: DOKUJE -> ZADOKOWANY" 20
kill "$pid"
ship_released "$pid"

# Statek uruchomiony ręcznie: generator o nim nie wie, a po kill -9 nikt nie usunie jego kolejki.
launch_ship wahadlowiec
pid=$ship_pid
wait_for_log "statek $pid: DOKUJE -> ZADOKOWANY" 40
kill -9 "$pid"
ship_released "$pid"

# Statek przy zatrzymaniu stacji dostaje zgodę na odlot, kończy się i usuwa swoją kolejkę.
launch_ship transportowiec
pid=$ship_pid
wait_for_log "statek $pid \(transportowiec\) zgłosił się" 2
kill -INT "$station_pid"
wait "$station_pid" || fail "station zakończył się błędem"
station_pid=""
wait_until 2 "statek uruchomiony ręcznie działa po zatrzymaniu stacji" ship_queue_gone "$pid"
no_ipc_left || fail "pozostały obiekty IPC"
echo "$scenario: OK"
