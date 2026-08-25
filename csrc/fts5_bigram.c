#include "sqlite3_fts5_bigram.h"
#include "unicode_bigram.h"

#include <limits.h>
#include <stddef.h>

#ifndef SQLITE_CORE
#include "sqlite3ext.h"
SQLITE_EXTENSION_INIT1
#endif

typedef struct BigramTokenizer {
    unsigned char allocated;
} BigramTokenizer;

typedef struct Fts5Callback {
    void *context;
    int (*token)(void *, int, const char *, int, int, int);
} Fts5Callback;

static void set_error(char **error, const char *message) {
    if (error != NULL) {
        *error = sqlite3_mprintf("%s", message);
    }
}

static int get_fts5_api(sqlite3 *db, fts5_api **api) {
    sqlite3_stmt *statement = NULL;
    int result;
    int finalize_result;

    *api = NULL;
    result = sqlite3_prepare_v2(db, "SELECT fts5(?1)", -1, &statement, NULL);
    if (result == SQLITE_OK) {
        result = sqlite3_bind_pointer(statement, 1, api, "fts5_api_ptr", NULL);
    }
    if (result == SQLITE_OK) {
        result = sqlite3_step(statement);
        if (result == SQLITE_ROW) {
            result = SQLITE_OK;
        }
    }
    finalize_result = sqlite3_finalize(statement);
    if (result == SQLITE_OK && finalize_result != SQLITE_OK) {
        result = finalize_result;
    }
    if (result == SQLITE_OK && *api == NULL) {
        result = SQLITE_ERROR;
    }
    return result;
}

static int tokenizer_create(
    void *unused,
    const char **arguments,
    int argument_count,
    Fts5Tokenizer **output
) {
    BigramTokenizer *tokenizer;
    (void)unused;
    (void)arguments;

    if (argument_count != 0 || output == NULL) {
        return SQLITE_ERROR;
    }
    tokenizer = sqlite3_malloc(sizeof(*tokenizer));
    if (tokenizer == NULL) {
        return SQLITE_NOMEM;
    }
    tokenizer->allocated = 1;
    *output = (Fts5Tokenizer *)tokenizer;
    return SQLITE_OK;
}

static void tokenizer_delete(Fts5Tokenizer *tokenizer) {
    sqlite3_free(tokenizer);
}

static int emit_token(
    void *context,
    const unsigned char *token,
    size_t token_bytes,
    size_t start,
    size_t end,
    size_t position
) {
    Fts5Callback *callback = context;
    (void)position;

    if (token_bytes > INT_MAX || start > INT_MAX || end > INT_MAX) {
        return SQLITE_TOOBIG;
    }
    return callback->token(
        callback->context,
        0,
        (const char *)token,
        (int)token_bytes,
        (int)start,
        (int)end
    );
}

static int tokenizer_tokenize(
    Fts5Tokenizer *tokenizer,
    void *context,
    int flags,
    const char *text,
    int text_bytes,
    const char *locale,
    int locale_bytes,
    int (*token)(void *, int, const char *, int, int, int)
) {
    Fts5Callback callback;
    int result;
    (void)tokenizer;
    (void)flags;
    (void)locale;
    (void)locale_bytes;

    if (text_bytes < 0 || token == NULL || (text == NULL && text_bytes != 0)) {
        return SQLITE_ERROR;
    }
    callback.context = context;
    callback.token = token;
    result = unicode_bigram_tokenize(
        (const unsigned char *)text,
        (size_t)text_bytes,
        emit_token,
        &callback
    );
    if (result == UNICODE_BIGRAM_INVALID_UTF8 || result == UNICODE_BIGRAM_RANGE) {
        return SQLITE_ERROR;
    }
    return result;
}

int sqlite3_fts5bigram_register(sqlite3 *db, char **error) {
    static fts5_tokenizer_v2 tokenizer = {
        2,
        tokenizer_create,
        tokenizer_delete,
        tokenizer_tokenize
    };
    fts5_api *api = NULL;
    int result;

    if (error != NULL) {
        *error = NULL;
    }
    if (db == NULL) {
        set_error(error, "unicode_bigram registration requires a SQLite connection");
        return SQLITE_MISUSE;
    }
    if (sqlite3_libversion_number() < 3047000) {
        set_error(error, "unicode_bigram requires SQLite 3.47.0 or newer");
        return SQLITE_ERROR;
    }
    result = get_fts5_api(db, &api);
    if (result != SQLITE_OK) {
        set_error(error, "could not obtain the SQLite FTS5 API");
        return result;
    }
    if (api->iVersion < 3 || api->xCreateTokenizer_v2 == NULL) {
        set_error(error, "SQLite FTS5 tokenizer v2 API is unavailable");
        return SQLITE_ERROR;
    }
    result = api->xCreateTokenizer_v2(
        api,
        "unicode_bigram",
        NULL,
        &tokenizer,
        NULL
    );
    if (result != SQLITE_OK) {
        set_error(error, "could not register the unicode_bigram tokenizer");
    }
    return result;
}

int sqlite3_fts5bigram_init(
    sqlite3 *db,
    char **error,
    const sqlite3_api_routines *api
) {
#ifndef SQLITE_CORE
    SQLITE_EXTENSION_INIT2(api);
#else
    (void)api;
#endif
    return sqlite3_fts5bigram_register(db, error);
}

