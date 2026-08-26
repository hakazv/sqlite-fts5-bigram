#include "sqlite3.h"
#include "sqlite3_fts5_bigram.h"
#include "unicode_bigram.h"
#include "unicode_lower.h"
#include "unicode_norm.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#ifndef SQLITE_CORE
#include "sqlite3ext.h"
SQLITE_EXTENSION_INIT1
#endif

typedef struct BigramTokenizer {
    unsigned char case_sensitive;
} BigramTokenizer;

typedef struct Fts5Callback {
    void *context;
    int (*token)(void *, int, const char *, int, int, int);
    int case_sensitive;
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
    int index;
    (void)unused;

    if (argument_count < 0 || argument_count % 2 != 0 || output == NULL) {
        return SQLITE_ERROR;
    }
    tokenizer = sqlite3_malloc(sizeof(*tokenizer));
    if (tokenizer == NULL) {
        return SQLITE_NOMEM;
    }
    tokenizer->case_sensitive = 0;
    for (index = 0; index < argument_count; index += 2) {
        if (arguments == NULL || arguments[index] == NULL ||
            arguments[index + 1] == NULL ||
            strcmp(arguments[index], "case_sensitive") != 0) {
            sqlite3_free(tokenizer);
            return SQLITE_ERROR;
        }
        if (strcmp(arguments[index + 1], "0") == 0) {
            tokenizer->case_sensitive = 0;
        } else if (strcmp(arguments[index + 1], "1") == 0) {
            tokenizer->case_sensitive = 1;
        } else {
            sqlite3_free(tokenizer);
            return SQLITE_ERROR;
        }
    }
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
    unsigned char folded[8];
    size_t folded_bytes;
    int result;
    (void)position;

    if (token_bytes > INT_MAX || start > INT_MAX || end > INT_MAX) {
        return SQLITE_TOOBIG;
    }
    if (!callback->case_sensitive) {
        result = unicode_lowercase_bigram(
            token,
            token_bytes,
            folded,
            &folded_bytes
        );
        if (result != UNICODE_BIGRAM_OK) {
            return SQLITE_ERROR;
        }
        token = folded;
        token_bytes = folded_bytes;
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
    BigramTokenizer *bigram_tokenizer = (BigramTokenizer *)tokenizer;
    int result;
    (void)flags;
    (void)locale;
    (void)locale_bytes;

    if (bigram_tokenizer == NULL || text_bytes < 0 || token == NULL ||
        (text == NULL && text_bytes != 0)) {
        return SQLITE_ERROR;
    }
    callback.context = context;
    callback.token = token;
    callback.case_sensitive = bigram_tokenizer->case_sensitive;

    /*
     * 正規化は窓を切る前に要る。分解済みの「ガ」は「カ + 結合濁点」の 2 コードポイントで、
     * コードポイントの窓は結合文字を独立した単位として切ってしまう。その結果「イド」が
     * 「イト」で引ける誤ヒットと、合成済みのクエリでの取りこぼしが同時に起きる。
     *
     * 既に正規化済みの本文 (大半) では確保も複製もせずに済む。
     */
    if (unicode_norm_is_already_composed((const unsigned char *)text, (size_t)text_bytes)) {
        result = unicode_bigram_tokenize(
            (const unsigned char *)text,
            (size_t)text_bytes,
            emit_token,
            &callback
        );
    } else {
        size_t capacity = (size_t)text_bytes;
        unsigned char *scratch = sqlite3_malloc64(capacity);
        unsigned int *starts = sqlite3_malloc64(sizeof(unsigned int) * (capacity + 1));
        unsigned int *ends = sqlite3_malloc64(sizeof(unsigned int) * (capacity + 1));

        if (scratch == NULL || starts == NULL || ends == NULL) {
            sqlite3_free(scratch);
            sqlite3_free(starts);
            sqlite3_free(ends);
            return SQLITE_NOMEM;
        }
        result = unicode_norm_tokenize(
            (const unsigned char *)text,
            (size_t)text_bytes,
            scratch,
            capacity,
            starts,
            ends,
            capacity + 1,
            emit_token,
            &callback
        );
        sqlite3_free(scratch);
        sqlite3_free(starts);
        sqlite3_free(ends);
    }
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
