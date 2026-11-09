/* Per-user install: files, Settings > Apps entry, autostart (Run), user PATH. No admin needed. */
#ifndef INSTALL_H
#define INSTALL_H
#include <windows.h>

typedef struct {
    wchar_t dir[MAX_PATH];      /* install directory */
    wchar_t uninst[260];        /* HKCU subkey of the Uninstall entry */
    wchar_t run[260];           /* HKCU subkey holding the autostart value */
    wchar_t env[260];           /* HKCU subkey holding Path */
    int manage_daemon;          /* stop/start the real daemon (off in tests) */
    int broadcast;              /* tell open windows PATH changed (off in tests) */
} stc_inst;

/* Defaults honour STC_INSTALL_DIR and STC_REG_ROOT (tests): the latter redirects all three keys. */
void stc_inst_defaults(stc_inst *i);
/* Copies the exes found in src_dir (skipped if same as install dir) and registers everything. */
int stc_install(const stc_inst *i, const wchar_t *src_dir);
/* purge also removes the data directory (database, config). */
int stc_uninstall(const stc_inst *i, int purge);
/* Prints a checklist; returns the number of problems. fix re-applies registration. */
int stc_doctor(const stc_inst *i, int fix, const wchar_t *src_dir);
int stc_daemon_running(void);
void stc_daemon_stop(void);
int stc_daemon_start(const wchar_t *dir);
#endif
