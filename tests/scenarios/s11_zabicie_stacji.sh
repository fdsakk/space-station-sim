#!/usr/bin/env bash
# S11: stacja zabita przez kill -9 nie może sprzątać, ale jej procesy potomne i statki kończą się
# same, a kolejny start usuwa pozostałości i działa normalnie, także z siecią. Druga stacja obok
# działającej nie startuje i niczego jej nie niszczy.
set -euo pipefail
cd "$(dirname "$0")/../.."
source tests/lib/scenario.sh

no_station_processes() {
    ! pgrep -x 'port|generator|gateway|ship' >/dev/null
}

start_station
wait_for_log 'gateway +start' 2
if ./build/station 2>/dev/null; then
    fail "druga stacja wystartowała obok działającej"
fi
[ -e /dev/shm/ss_state ] || fail "druga stacja usunęła obiekty działającej"
./build/panel fala
wait_for_log 'generator +uruchomiono statek' 2

kill -9 "$station_pid"
wait "$station_pid" || true
station_pid=""
wait_until 3 "procesy stacji działają dalej po jej zabiciu" no_station_processes
[ -e /dev/shm/ss_state ] || fail "po kill -9 nie ma czego sprzątać: scenariusz niczego nie sprawdza"

start_station
wait_for_log 'gateway +start' 2
reply=$(echo 'statek awaryjny' | ./build/operator)
[[ "$reply" == *'stacja: ok'* ]] || fail "brama po ponownym starcie nie działa: $reply"
stop_station
echo "$scenario: OK"
