#!/usr/bin/env bash
# S3: awaria portu (komendy operatora). Awaria przy samej rezerwacji odsyła statek na początek
# kolejki. Awaria w trakcie obsługi zamyka śluzę, przerywa obsługę i też odsyła statek do kolejki,
# a pozostałe porty pracują. Po naprawie port jest wolny, a statek zostaje obsłużony w innym porcie.
# Na koniec operator zatrzymuje stację komendą shutdown.
set -euo pipefail
cd "$(dirname "$0")/../.."
source tests/lib/scenario.sh

# Numer portu przydzielonego statkowi (z wpisu sterownika w dzienniku).
port_of() {
    grep -E "przydzielony statkowi $1\$" stacja.log | head -1 | awk '{print $4}'
}

start_station
# Na początku porty są wolne. Statek z --no-ack zostaje przy samej rezerwacji, więc awaria
# trafia w stan ZAREZERWOWANY.
launch_ship wahadlowiec --no-ack
waiting=$ship_pid
wait_for_log "przydzielony statkowi $waiting\$" 2
reserved=$(port_of "$waiting")
./build/panel awaria "$reserved"
wait_for_log "port $reserved +port: ZAREZERWOWANY -> AWARIA" 2
wait_for_log "statek $waiting wraca do kolejki \(awaria portu $reserved\)" 2
./build/panel napraw "$reserved"
wait_for_log "port $reserved +port: AWARIA -> NAPRAWA" 2

launch_ship wahadlowiec
pid=$ship_pid
wait_for_log "przydzielony statkowi $pid\$" 20
port=$(port_of "$pid")
wait_for_log "port $port +obsługa: [A-Z_]+ -> TRWA" 20

./build/panel awaria "$port"
wait_for_log "port $port +port: ZADOKOWANY -> AWARIA" 2
log_has "port $port +śluza: OTWARTA -> ZAMKNIETA" || fail "śluza nie zamknęła się przy awarii"
log_has "port $port +obsługa: TRWA -> BRAK" || fail "obsługa nie została przerwana"
wait_for_log "statek $pid wraca do kolejki \(awaria portu $port\)" 2

# Pozostałe porty pracują: nowy statek dokuje w innym porcie w trakcie awarii.
launch_ship wahadlowiec
other=$ship_pid
wait_for_log "statek $other: DOKUJE -> ZADOKOWANY" 60
log_has "port $port przydzielony statkowi $other\$" && fail "zepsuty port dostał statek"

./build/panel napraw "$port"
wait_for_log "port $port +port: AWARIA -> NAPRAWA" 2
wait_for_log "port $port +port: NAPRAWA -> WOLNY" 30
wait_for_log "statek $pid: ODDOKOWUJE -> ODLECIAL" 40

# Awaria w trakcie oddokowania: statek nie wraca do kolejki, tylko od razu odlatuje.
launch_ship wahadlowiec
leaving=$ship_pid
wait_for_log "statek $leaving: ZADOKOWANY -> ODDOKOWUJE" 60
leaving_port=$(port_of "$leaving")
./build/panel awaria "$leaving_port"
wait_for_log "port $leaving_port +port: ODDOKOWANIE -> AWARIA" 2
wait_for_log "statek $leaving: ODDOKOWUJE -> ODLECIAL" 2

./build/panel shutdown
wait_for_log 'operator: zatrzymanie stacji' 2
finish_station
echo "$scenario: OK"
