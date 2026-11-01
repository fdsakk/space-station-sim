#!/usr/bin/env bash
# S2: fala zgłoszeń - trzeci port czeka na energię (2 jednostki na 3 porty), a statek awaryjny
# wyprzedza zwykły statek, który zgłosił się wcześniej. Na koniec komendy operatora "statek"
# i "fala" uruchamiają statki w generatorze.
set -euo pipefail
cd "$(dirname "$0")/../.."
source tests/lib/scenario.sh

# Ile razy generator uruchomił statek albo odmówił z powodu limitu.
spawn_attempts() {
    grep -cE 'generator +(uruchomiono statek|limit)' stacja.log || true
}

at_least_attempts() {
    [ "$(spawn_attempts)" -ge "$1" ]
}

# Numer wiersza dziennika z przydziałem portu dla statku; pusty, gdy przydziału jeszcze nie było.
assigned_line() {
    grep -n "przydzielony statkowi $1\$" stacja.log | cut -d: -f1 || true
}

start_station
for i in 1 2 3; do
    launch_ship transportowiec
    wait_for_log "przydzielony statkowi $ship_pid\$" 2
done
launch_ship wahadlowiec
normal=$ship_pid
wait_for_log "statek $normal \(wahadlowiec\) zgłosił się" 2
launch_ship awaryjny
emergency=$ship_pid
wait_for_log "statek $emergency \(awaryjny\) zgłosił się" 2

wait_for_log 'obsługa: CZEKA_NA_ENERGIE -> TRWA' 20
wait_for_log "przydzielony statkowi $emergency\$" 30
normal_line=$(assigned_line "$normal")
if [ -n "$normal_line" ] && [ "$normal_line" -lt "$(assigned_line "$emergency")" ]; then
    fail "statek awaryjny nie wyprzedził zwykłego"
fi

# Statek i fala (WAVE_SIZE = 5) z panelu: co najmniej 6 nowych prób uruchomienia statku. Losowy
# statek generatora pojawia się najwyżej co 3 s, więc nie zastąpi brakującej komendy.
before=$(spawn_attempts)
./build/panel statek transportowiec
./build/panel fala
wait_for_log 'operator: fala' 2
wait_until 2 "generator nie wykonał komend statek i fala" at_least_attempts $((before + 6))

stop_station
echo "$scenario: OK"
