# Build with: mingw32-make   (gcc from MinGW)
CC      = gcc
CFLAGS  = -std=gnu11 -Os -Wall -Wextra -ffunction-sections -fdata-sections
LDFLAGS = -s -Wl,--gc-sections
SQLFLAGS = -Os -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_DEFAULT_MEMSTATUS=0 \
           -DSQLITE_OMIT_DEPRECATED -DSQLITE_OMIT_SHARED_CACHE -DSQLITE_DQS=0 -DSQLITE_OMIT_PROGRESS_CALLBACK \
           -DSQLITE_DEFAULT_WAL_SYNCHRONOUS=1 -ffunction-sections -fdata-sections
DSQLFLAGS = $(SQLFLAGS)             -DSQLITE_OMIT_FOREIGN_KEY -DSQLITE_OMIT_AUTHORIZATION -DSQLITE_OMIT_JSON             -DSQLITE_OMIT_COMPOUND_SELECT             -DSQLITE_OMIT_EXPLAIN -DSQLITE_OMIT_GET_TABLE -DSQLITE_OMIT_INTEGRITY_CHECK             -DSQLITE_OMIT_TRACE -DSQLITE_OMIT_UTF16 -DSQLITE_OMIT_BLOB_LITERAL -DSQLITE_OMIT_COMPLETE             -DSQLITE_OMIT_TCL_VARIABLE -DSQLITE_OMIT_DECLTYPE -DSQLITE_OMIT_AUTOVACUUM -DSQLITE_OMIT_SHARED_CACHE             -DSQLITE_DEFAULT_CACHE_SIZE=-64 -DSQLITE_MAX_EXPR_DEPTH=0 -DSQLITE_LIKE_DOESNT_MATCH_BLOBS
DCFLAGS = $(CFLAGS) -fno-asynchronous-unwind-tables -fno-ident
B = build

COREO = $(B)/db.o $(B)/hb.o $(B)/span.o $(B)/state.o $(B)/sched.o $(B)/query.o $(B)/scan.o $(B)/config.o
CORE = $(COREO) $(B)/sqlite3.o
# the daemon links a trimmed SQLite: it only inserts, updates, runs pragmas, migrations and VACUUM INTO
DCORE = $(COREO) $(B)/sqlite3d.o
INS = $(wildcard src/cli/insights/*.c)
INSH = $(wildcard src/cli/insights/*.h) src/cli/render.h
INST = $(B)/install.o

all: $(B)/screen-time-cli.exe $(B)/screen-time-daemon.exe

$(B):
	mkdir -p $(B)

$(B)/sqlite3.o: vendor/sqlite3.c | $(B)
	$(CC) $(SQLFLAGS) -c $< -o $@

$(B)/sqlite3d.o: vendor/sqlite3.c | $(B)
	$(CC) $(DSQLFLAGS) -fno-asynchronous-unwind-tables -fno-ident -c $< -o $@

$(B)/%.o: src/core/%.c src/core/stc.h | $(B)
	$(CC) $(CFLAGS) -c $< -o $@

$(B)/install.o: src/install/install.c src/install/install.h src/core/stc.h | $(B)
	$(CC) $(CFLAGS) -c $< -o $@

$(B)/screen-time-cli.exe: src/cli/main.c src/cli/update.c src/cli/render.c $(INS) $(INSH) $(CORE) $(INST)
	$(CC) $(CFLAGS) $(filter %.c %.o,$^) -o $@ $(LDFLAGS) -lshlwapi

# ole32/uuid are not linked: media.c loads them only while probing
$(B)/screen-time-daemon.exe: src/daemon/main.c src/daemon/media.c $(DCORE)
	$(CC) $(DCFLAGS) -mwindows $^ -o $@ $(LDFLAGS) -lwtsapi32

# daemon with per-minute counters written to meta('profile'); used by the bench
$(B)/screen-time-daemon-profile.exe: src/daemon/main.c src/daemon/media.c $(DCORE)
	$(CC) $(DCFLAGS) -DSTC_PROFILE -mwindows $^ -o $@ $(LDFLAGS) -lwtsapi32

profile: $(B)/screen-time-daemon-profile.exe

$(B)/test_core.exe: tests/test_core.c $(CORE)
	$(CC) $(CFLAGS) $^ -o $@

# same tests against the trimmed SQLite the daemon uses (migrations, backup via VACUUM INTO, tracker statements)
$(B)/test_core_small.exe: tests/test_core.c $(DCORE)
	$(CC) $(CFLAGS) $^ -o $@

$(B)/test_insights.exe: tests/test_insights.c src/cli/render.c $(filter-out src/cli/insights/registry.c,$(INS)) $(INSH) $(CORE)
	$(CC) $(CFLAGS) $(filter %.c %.o,$^) src/cli/insights/registry.c -o $@

$(B)/test_registry.exe: tests/test_registry.c $(CORE) $(INST)
	$(CC) $(CFLAGS) $^ -o $@ -lshlwapi

test: $(B)/test_core.exe $(B)/test_core_small.exe $(B)/test_insights.exe $(B)/test_registry.exe all
	$(B)/test_core.exe
	$(B)/test_core_small.exe
	$(B)/test_insights.exe
	$(B)/test_registry.exe

bench:
	powershell -NoProfile -ExecutionPolicy Bypass -File tools/bench.ps1

clean:
	rm -rf $(B)

.PHONY: all test bench clean profile
