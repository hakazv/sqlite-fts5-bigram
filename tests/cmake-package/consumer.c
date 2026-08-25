#include <sqlite3_fts5_bigram.h>

int use_installed_header(struct sqlite3 *connection) {
    return sqlite3_fts5bigram_register(connection, 0);
}
