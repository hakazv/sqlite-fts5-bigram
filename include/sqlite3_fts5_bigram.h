#ifndef SQLITE3_FTS5_BIGRAM_H
#define SQLITE3_FTS5_BIGRAM_H

#include "sqlite3.h"

#if defined(_WIN32) && !defined(SQLITE_CORE)
#define FTS5_BIGRAM_API __declspec(dllexport)
#else
#define FTS5_BIGRAM_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
** Register the unicode_bigram tokenizer on db. On error, *error is either
** NULL or an sqlite3_malloc()-allocated message owned by the caller.
*/
FTS5_BIGRAM_API int sqlite3_fts5bigram_register(sqlite3 *db, char **error);

/* Loadable-extension entry point. */
FTS5_BIGRAM_API int sqlite3_fts5bigram_init(
    sqlite3 *db,
    char **error,
    const sqlite3_api_routines *api
);

#ifdef __cplusplus
}
#endif

#endif

