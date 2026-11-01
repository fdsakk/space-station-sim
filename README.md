# Stacja kosmiczna — projekt z Informatyki przemysłowej

Symulacja systemu sterowania stacją kosmiczną z 3 portami dokującymi: procesy POSIX,
pamięć dzielona, semafory i kolejki komunikatów, zdalny operator przez UDP z HMAC.

## Wymagania

Linux albo WSL2 (kolejki komunikatów POSIX nie działają na macOS).

- Arch: `sudo pacman -S --needed gcc make clang cppcheck openssl ncurses`
- Ubuntu / Debian / WSL2: `sudo apt install build-essential clang-format cppcheck libssl-dev libncurses-dev`

## Budowanie i uruchomienie

Wszystkie polecenia z katalogu głównego repozytorium.

```bash
make                 # buduje programy do build/
./build/station      # uruchamia stację; Ctrl+C zatrzymuje i sprząta
./build/station 0.0.0.0  # to samo, a brama sieciowa przyjmuje pakiety z innych komputerów
tail -f stacja.log   # dziennik zdarzeń (w drugim terminalu)
./build/panel        # panel operatora (w drugim terminalu); Ctrl+C zamyka panel
./build/panel fala   # jedna komenda bez ekranu: awaria <1-3>, napraw <1-3>, statek <typ>, fala, shutdown
./build/operator     # zdalny operator przez UDP (te same komendy); ./build/operator <adres stacji>
```

`make` przy pierwszym budowaniu tworzy losowy klucz HMAC `config/klucz` (poza repozytorium).
Zdalny operator na innym komputerze potrzebuje kopii tego pliku.

## Testy i jakość

```bash
make test            # testy jednostkowe i scenariusze
make check           # to samo z sanitizerami + clang-format + cppcheck (przed każdym commitem)
make format          # formatuje kod
make clean-ipc       # usuwa obiekty IPC po stacji zabitej przez kill -9
```
