#!/usr/bin/env bash
# S5: proces portu zabity (kill -9) w trakcie obsługi - sterownik przełącza port w AWARIA i
# odzyskuje jego energię, a statek wraca do kolejki; "napraw" uruchamia nowy proces portu, który
# przejmuje rekord z pamięci dzielonej i naprawia port.
set -euo pipefail
cd "$(dirname "$0")/../.."
source tests/lib/scenario.sh

# Pid procesu portu z ostatniego wpisu "uruchomiono port N (pid P)".
port_process() {
    grep -E "uruchomiono port $1 \(pid" stacja.log | tail -1 | grep -oE '[0-9]+\)$' | tr -d ')'
}

port_restarted() {
    [ "$(grep -cE "uruchomiono port $1 \(pid" stacja.log)" -ge 2 ]
}

start_station
launch_ship wahadlowiec
pid=$ship_pid
wait_for_log "przydzielony statkowi $pid\$" 2
port=$(grep -E "przydzielony statkowi $pid\$" stacja.log | awk '{print $4}')
wait_for_log "port $port +obsługa: [A-Z_]+ -> TRWA" 20

old=$(port_process "$port")
kill -9 "$old"
wait_for_log "port $port \(pid $old\) zabity sygnałem 9" 2
wait_for_log "port $port: proces nie działa - port w awarii, energia odzyskana" 2
wait_for_log "statek $pid wraca do kolejki \(awaria portu $port\)" 2

# Awaria martwego portu niczego nie zmienia (kolejki martwego portu nikt nie czyta).
./build/panel awaria "$port"
wait_for_log "port $port: proces nie działa, port już jest w awarii" 2

./build/panel napraw "$port"
wait_until 2 "port $port nie został uruchomiony ponownie" port_restarted "$port"
[ "$(port_process "$port")" != "$old" ] || fail "nowy proces portu ma stary pid"
wait_for_log "port $port +port: AWARIA -> NAPRAWA" 2
wait_for_log "port $port +port: NAPRAWA -> WOLNY" 30
wait_for_log "statek $pid: ODDOKOWUJE -> ODLECIAL" 40

stop_station
echo "$scenario: OK"
