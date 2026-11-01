#!/usr/bin/env bash
# S8: zawieszony proces portu nie zatrzymuje stacji. Port zatrzymany sygnałem SIGSTOP nie
# czyta swojej kolejki. Komendy dla niego zapełniają ją, a sterownik pomija nadmiarowe polecenia
# zamiast czekać i dalej obsługuje inne komendy. Po SIGCONT port wraca do pracy.
set -euo pipefail
cd "$(dirname "$0")/../.."
source tests/lib/scenario.sh

start_station
port_pid=$(pgrep -f '^port 1$')
kill -STOP "$port_pid"

# Pojemność kolejki to 10 komunikatów, więc kilka ostatnich komend musi zostać pominiętych.
for _ in $(seq 12); do
    ./build/panel awaria 1
done
wait_for_log 'station +port 1 nie odbiera \(kolejka pełna\) - pominięto CMD_FAULT' 2

# Sterownik nie czeka na zawieszony port: następna komenda jest obsłużona od razu.
./build/panel statek awaryjny
wait_for_log 'station +operator: statek awaryjny' 2

kill -CONT "$port_pid"
wait_for_log 'port 1 +port: [A-Z]+ -> AWARIA' 2

./build/panel shutdown
wait_for_log 'operator: zatrzymanie stacji' 2
finish_station
echo "$scenario: OK"
