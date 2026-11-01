# Budowanie programów stacji, testy i kontrola jakości. Uruchamiać z katalogu głównego repo.

CC = gcc
CFLAGS = -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic -Werror -g $(EXTRA_CFLAGS)
LDLIBS = -lrt -lpthread
SANITIZE = -fsanitize=address,undefined -fno-omit-frame-pointer
BUILD = build
OBJ = $(BUILD)/obj

COMMON_OBJ = $(patsubst src/%.c,$(OBJ)/%.o,$(wildcard src/common/*.c))
UNIT_TESTS = $(patsubst tests/unit/%.c,$(BUILD)/tests/%,$(wildcard tests/unit/*.c))
SCENARIOS = $(wildcard tests/scenarios/*.sh)
SOURCES = $(wildcard src/*/*.c src/*/*.h tests/unit/*.c)

# Programy: każdy program dopisuje tu swój blok (nazwa w BINS + własne pliki obiektowe).
BINS =

BINS += $(BUILD)/station
$(BUILD)/station: $(OBJ)/station/main.o $(OBJ)/station/supervisor.o $(OBJ)/station/control.o \
	$(OBJ)/station/queue.o $(OBJ)/port/port_fsm.o

BINS += $(BUILD)/port
$(BUILD)/port: $(OBJ)/port/main.o $(OBJ)/port/port_fsm.o

# Testy jednostkowe modułów spoza src/common/: dodatkowe pliki obiektowe.
$(BUILD)/tests/test_queue: $(OBJ)/station/queue.o
$(BUILD)/tests/test_port_fsm: $(OBJ)/port/port_fsm.o

.PHONY: all test check format clean clean-ipc

# Nieudana reguła usuwa swój cel: np. po błędzie openssl nie zostaje pusty config/klucz, który
# make uznałby potem za gotowy.
.DELETE_ON_ERROR:

# Bloki programów stoją przed regułą all, więc bez tego domyślnym celem byłby pierwszy program.
.DEFAULT_GOAL := all

all: $(BINS)

# Każdy program = własne obiekty + wspólne obiekty z src/common/.
$(BINS): $(COMMON_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(OBJ)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/tests/%: tests/unit/%.c $(COMMON_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

test: all $(UNIT_TESTS)
	@for t in $(UNIT_TESTS); do ./$$t || exit 1; done
	@for s in $(SCENARIOS); do bash $$s || exit 1; done

check:
	$(MAKE) clean
	$(MAKE) test EXTRA_CFLAGS="$(SANITIZE)"
	clang-format --dry-run --Werror $(SOURCES)
	cppcheck --enable=warning,style,unusedFunction --error-exitcode=1 \
		--suppress=missingIncludeSystem --quiet src tests/unit
	$(MAKE) clean

format:
	clang-format -i $(SOURCES)

clean:
	rm -rf $(BUILD)

# Usuwa obiekty IPC po stacji zabitej bez sprzątania (np. kill -9). Nie przy działającej stacji!
clean-ipc:
	rm -f /dev/shm/ss_* /dev/shm/sem.ss_* /dev/mqueue/ss_*

-include $(wildcard $(OBJ)/*/*.d)
