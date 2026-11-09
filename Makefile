# Build with: mingw32-make   (gcc from MinGW)
CC      = gcc
CFLAGS  = -std=gnu11 -Os -Wall -Wextra -ffunction-sections -fdata-sections
LDFLAGS = -s -Wl,--gc-sections
SQLFLAGS = -Os -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_DEFAULT_MEMSTATUS=0 \
           -DSQLITE_OMIT_DEPRECATED -DSQLITE_OMIT_SHARED_CACHE -DSQLITE_DQS=0 -DSQLITE_OMIT_PROGRESS_CALLBACK \
           -DSQLITE_DEFAULT_WAL_SYNCHRONOUS=1 -ffunction-sections -fdata-sections
B = build

CORE = $(B)/db.o $(B)/span.o $(B)/state.o $(B)/query.o $(B)/sqlite3.o
INST = $(B)/install.o

all: $(B)/screen-time-cli.exe $(B)/screen-time-daemon.exe

$(B):
	mkdir -p $(B)

$(B)/sqlite3.o: vendor/sqlite3.c | $(B)
	$(CC) $(SQLFLAGS) -c $< -o $@

$(B)/%.o: src/core/%.c src/core/stc.h | $(B)
	$(CC) $(CFLAGS) -c $< -o $@

$(B)/install.o: src/install/install.c src/install/install.h src/core/stc.h | $(B)
	$(CC) $(CFLAGS) -c $< -o $@

$(B)/screen-time-cli.exe: src/cli/main.c src/cli/update.c $(CORE) $(INST)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS) -lshlwapi

$(B)/screen-time-daemon.exe: src/daemon/main.c src/daemon/media.c $(CORE)
	$(CC) $(CFLAGS) -mwindows $^ -o $@ $(LDFLAGS) -lole32 -luuid -lwtsapi32

$(B)/test_core.exe: tests/test_core.c $(CORE)
	$(CC) $(CFLAGS) $^ -o $@

$(B)/test_registry.exe: tests/test_registry.c $(CORE) $(INST)
	$(CC) $(CFLAGS) $^ -o $@ -lshlwapi

test: $(B)/test_core.exe $(B)/test_registry.exe all
	$(B)/test_core.exe
	$(B)/test_registry.exe

bench:
	powershell -NoProfile -ExecutionPolicy Bypass -File tools/bench.ps1

clean:
	rm -rf $(B)

.PHONY: all test bench clean
