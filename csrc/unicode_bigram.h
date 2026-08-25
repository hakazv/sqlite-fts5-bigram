#ifndef UNICODE_BIGRAM_H
#define UNICODE_BIGRAM_H

#include <stddef.h>

enum unicode_bigram_result {
    UNICODE_BIGRAM_OK = 0,
    UNICODE_BIGRAM_INVALID_UTF8 = -1,
    UNICODE_BIGRAM_RANGE = -2
};

typedef int (*unicode_bigram_callback)(
    void *context,
    const unsigned char *token,
    size_t token_bytes,
    size_t start,
    size_t end,
    size_t position
);

int unicode_bigram_tokenize(
    const unsigned char *text,
    size_t text_bytes,
    unicode_bigram_callback callback,
    void *context
);

#endif

