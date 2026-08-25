#include "unicode_bigram.h"

#include <stdint.h>

static int utf8_code_point_bytes(
    const unsigned char *text,
    size_t remaining,
    size_t *length
) {
    unsigned char first;

    if (remaining == 0 || text == NULL || length == NULL) {
        return UNICODE_BIGRAM_INVALID_UTF8;
    }
    first = text[0];
    if (first <= 0x7f) {
        *length = 1;
        return UNICODE_BIGRAM_OK;
    }
    if (first >= 0xc2 && first <= 0xdf) {
        if (remaining < 2 || text[1] < 0x80 || text[1] > 0xbf) {
            return UNICODE_BIGRAM_INVALID_UTF8;
        }
        *length = 2;
        return UNICODE_BIGRAM_OK;
    }
    if (first >= 0xe0 && first <= 0xef) {
        if (remaining < 3 || text[2] < 0x80 || text[2] > 0xbf) {
            return UNICODE_BIGRAM_INVALID_UTF8;
        }
        if ((first == 0xe0 && (text[1] < 0xa0 || text[1] > 0xbf)) ||
            (first == 0xed && (text[1] < 0x80 || text[1] > 0x9f)) ||
            (first != 0xe0 && first != 0xed &&
             (text[1] < 0x80 || text[1] > 0xbf))) {
            return UNICODE_BIGRAM_INVALID_UTF8;
        }
        *length = 3;
        return UNICODE_BIGRAM_OK;
    }
    if (first >= 0xf0 && first <= 0xf4) {
        if (remaining < 4 || text[2] < 0x80 || text[2] > 0xbf ||
            text[3] < 0x80 || text[3] > 0xbf) {
            return UNICODE_BIGRAM_INVALID_UTF8;
        }
        if ((first == 0xf0 && (text[1] < 0x90 || text[1] > 0xbf)) ||
            (first == 0xf4 && (text[1] < 0x80 || text[1] > 0x8f)) ||
            (first != 0xf0 && first != 0xf4 &&
             (text[1] < 0x80 || text[1] > 0xbf))) {
            return UNICODE_BIGRAM_INVALID_UTF8;
        }
        *length = 4;
        return UNICODE_BIGRAM_OK;
    }
    return UNICODE_BIGRAM_INVALID_UTF8;
}

static int validate_utf8(const unsigned char *text, size_t text_bytes) {
    size_t offset = 0;

    if (text_bytes > 0 && text == NULL) {
        return UNICODE_BIGRAM_INVALID_UTF8;
    }
    while (offset < text_bytes) {
        size_t length = 0;
        int result = utf8_code_point_bytes(text + offset, text_bytes - offset, &length);
        if (result != UNICODE_BIGRAM_OK) {
            return result;
        }
        if (length > SIZE_MAX - offset) {
            return UNICODE_BIGRAM_RANGE;
        }
        offset += length;
    }
    return UNICODE_BIGRAM_OK;
}

int unicode_bigram_tokenize(
    const unsigned char *text,
    size_t text_bytes,
    unicode_bigram_callback callback,
    void *context
) {
    size_t first_start = 0;
    size_t next_start;
    size_t first_length = 0;
    size_t position = 0;
    int result;

    result = validate_utf8(text, text_bytes);
    if (result != UNICODE_BIGRAM_OK || text_bytes == 0) {
        return result;
    }
    if (callback == NULL) {
        return UNICODE_BIGRAM_RANGE;
    }
    result = utf8_code_point_bytes(text, text_bytes, &first_length);
    if (result != UNICODE_BIGRAM_OK || first_length == text_bytes) {
        return result;
    }
    next_start = first_length;
    while (next_start < text_bytes) {
        size_t second_length = 0;
        size_t end;
        int callback_result;

        result = utf8_code_point_bytes(
            text + next_start,
            text_bytes - next_start,
            &second_length
        );
        if (result != UNICODE_BIGRAM_OK || second_length > SIZE_MAX - next_start) {
            return result == UNICODE_BIGRAM_OK ? UNICODE_BIGRAM_RANGE : result;
        }
        end = next_start + second_length;
        callback_result = callback(
            context,
            text + first_start,
            end - first_start,
            first_start,
            end,
            position
        );
        if (callback_result != UNICODE_BIGRAM_OK) {
            return callback_result;
        }
        if (position == SIZE_MAX) {
            return UNICODE_BIGRAM_RANGE;
        }
        position += 1;
        first_start = next_start;
        next_start = end;
    }
    return UNICODE_BIGRAM_OK;
}

