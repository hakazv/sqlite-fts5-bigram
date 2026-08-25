#include <sqlite3_fts5_bigram.h>

int use_installed_header_from_cpp(sqlite3 *connection) {
    return sqlite3_fts5bigram_register(connection, nullptr);
}
