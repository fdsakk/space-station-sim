# Wspólne funkcje scenariuszy: start i zatrzymanie stacji, czekanie na wpisy w dzienniku.
# Użycie w skrypcie z tests/scenarios/: source tests/lib/scenario.sh (z katalogu głównego repo).

scenario=$(basename "$0" .sh)
station_pid=""
ship_pid=""
ships=() # statki uruchomione ręcznie przez scenariusz (nie są dziećmi generatora)

fail() {
    echo "$scenario: BŁĄD - $1"
    exit 1
}

# Po nieudanej asercji nic nie może zostać w tle z obiektami IPC (kolejny test by nie ruszył).
cleanup() {
    for pid in "${ships[@]}"; do
        kill "$pid" 2>/dev/null || true
    done
    # Statki generatora też: stacja zawieszona na ich zakończeniu nie pozwoliłaby wrócić z wait.
    pkill -TERM -x ship 2>/dev/null || true
    if [ -n "$station_pid" ]; then
        kill -INT "$station_pid" 2>/dev/null || true
    fi
    wait 2>/dev/null || true
}

log_has() {
    grep -qE "$1" stacja.log 2>/dev/null
}

# wait_until <sekundy> <opis błędu> <polecenie...>: ponawia polecenie co 0,2 s, aż się uda.
wait_until() {
    local limit=$1 what=$2
    shift 2
    local deadline=$((SECONDS + limit))
    until "$@"; do
        [ "$SECONDS" -lt "$deadline" ] || fail "$what (czekano $limit s)"
        sleep 0.2
    done
}

wait_for_log() {
    wait_until "$2" "brak wpisu '$1' w stacja.log" log_has "$1"
}

start_station() {
    rm -f stacja.log
    trap cleanup EXIT
    ./build/station &
    station_pid=$!
    wait_for_log 'generator +start' 2
}

# launch_ship <typ> [--no-ack]: uruchamia statek obok generatora; pid trafia do ship_pid.
launch_ship() {
    ./build/ship "$@" &
    ship_pid=$!
    ships+=("$ship_pid")
}

no_ipc_left() {
    ! ls /dev/shm /dev/mqueue | grep -q 'ss_'
}

stop_station() {
    kill -INT "$station_pid"
    finish_station
}

# Czeka na koniec stacji (po sygnale albo komendzie shutdown) i sprawdza sprzątanie IPC.
finish_station() {
    wait "$station_pid" || fail "station zakończył się błędem"
    station_pid=""
    for pid in "${ships[@]}"; do
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    done
    ships=()
    no_ipc_left || fail "pozostały obiekty IPC: $(ls /dev/shm /dev/mqueue | grep 'ss_' | tr '\n' ' ')"
}
