/* `screen-time-cli label`: names, categories and topics per exe (labels.ini). */
#ifndef LABEL_H
#define LABEL_H
#include "../core/stc.h"
int cmd_label(sqlite3 *db, int argc, char **argv);
#endif
