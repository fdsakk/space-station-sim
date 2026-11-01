#!/usr/bin/env bash
# S7: fałszywy pakiet sieciowy. Komenda z programu operator przechodzi przez bramę. Pakiety ze
# złym podpisem, z podmienioną komendą, powtórzone, stare i za długie brama odrzuca i opisuje
# w dzienniku, a powtórzona komenda wykonuje się tylko raz. Na koniec operator zatrzymuje stację
# przez sieć.
set -euo pipefail
cd "$(dirname "$0")/../.."
source tests/lib/scenario.sh

key=$(cat config/klucz)

# signed_packet <znacznik_ms> <komenda>: pakiet podpisany kluczem stacji. Podpis liczy program
# openssl, a nie nasz kod: tak wyglądałby pakiet przechwycony z sieci.
signed_packet() {
    local text="$1 $2"
    local mac
    mac=$(printf '%s' "$text" | openssl dgst -sha256 -hmac "$key" | awk '{print $NF}')
    printf '%s %s' "$text" "$mac"
}

# Wysyła jeden pakiet UDP do bramy (przekierowanie /dev/udp to funkcja basha).
send_udp() {
    printf '%s' "$1" >/dev/udp/127.0.0.1/5555
}

now_ms() {
    date +%s%3N
}

rejected() {
    wait_for_log "gateway +odrzucono pakiet z 127.0.0.1: $1" 2
}

start_station
wait_for_log 'gateway +start, UDP 127.0.0.1:5555' 2

# Prawdziwy operator: brama sprawdza podpis i przekazuje komendę sterownikowi. Komendy z potoku
# wychodzą tuż po sobie, a każda musi mieć większy znacznik czasu, inaczej brama uznałaby ją
# za powtórzenie.
reply=$(printf 'statek awaryjny\nawaria 7\nfala\n' | ./build/operator)
[[ "$reply" == *"stacja: ok"*"stacja: nieznana komenda"*"stacja: ok"* ]] ||
    fail "zła odpowiedź na komendy operatora: $reply"
wait_for_log 'station +operator: statek awaryjny' 2
wait_for_log 'station +operator: fala' 2

# Podpis bez znajomości klucza.
send_udp "$(now_ms) awaria 1 $(printf '0%.0s' {1..64})"
rejected 'zły podpis'

# Przechwycony pakiet z podmienioną komendą: podpis przestaje pasować.
packet=$(signed_packet "$(now_ms)" "awaria 1")
send_udp "${packet/awaria 1/awaria 2}"
wait_until 2 "podmieniona komenda nie została odrzucona" \
    test "$(grep -c 'odrzucono pakiet z 127.0.0.1: zły podpis' stacja.log)" -eq 2

# Powtórzenie poprawnego pakietu: wykonuje się tylko pierwszy.
packet=$(signed_packet "$(now_ms)" "statek wahadlowiec")
send_udp "$packet"
wait_for_log 'station +operator: statek wahadlowiec' 2
send_udp "$packet"
rejected 'powtórzony'
[ "$(grep -c 'operator: statek wahadlowiec' stacja.log)" -eq 1 ] || fail "powtórzona komenda wykonana"

# Pakiet sprzed 60 s (poza oknem 30 s) i pakiet za długi.
send_udp "$(signed_packet "$(($(now_ms) - 60000))" fala)"
rejected 'znacznik czasu poza oknem'
send_udp "$(printf 'a%.0s' {1..300})"
rejected 'za długi'
send_udp "awaria 1"
rejected 'zły format'

log_has 'port 1 +port: [A-Z]+ -> AWARIA' && fail "port 1 w awarii po odrzuconym pakiecie"
[ "$(grep -c 'operator: fala' stacja.log)" -eq 1 ] || fail "wykonano komendę ze starego pakietu"

reply=$(echo shutdown | ./build/operator)
[[ "$reply" == *"stacja: ok"* ]] || fail "brak odpowiedzi na shutdown: $reply"
wait_for_log 'station +operator: zatrzymanie stacji' 2
finish_station
echo "$scenario: OK"
