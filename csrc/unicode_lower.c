#include "unicode_lower.h"

#include "unicode_bigram.h"

#include <stdint.h>

typedef struct UnicodeLowerMapping {
    uint32_t upper;
    uint32_t lower;
} UnicodeLowerMapping;

static const UnicodeLowerMapping unicode_lower_mappings[] = {
#include "unicode_lower_table.inc"
};

static uint32_t lowercase_code_point(uint32_t code_point) {
    size_t low = 0;
    size_t high = sizeof(unicode_lower_mappings) / sizeof(unicode_lower_mappings[0]);

    while (low < high) {
        size_t middle = low + (high - low) / 2;
        uint32_t upper = unicode_lower_mappings[middle].upper;
        if (code_point < upper) {
            high = middle;
        } else if (code_point > upper) {
            low = middle + 1;
        } else {
            return unicode_lower_mappings[middle].lower;
        }
    }
    return code_point;
}

static int decode_utf8(
    const unsigned char *input,
    size_t remaining,
    uint32_t *code_point,
    size_t *length
) {
    unsigned char first;

    if (input == NULL || code_point == NULL || length == NULL || remaining == 0) {
        return UNICODE_BIGRAM_INVALID_UTF8;
    }
    first = input[0];
    if (first <= 0x7f) {
        *code_point = first;
        *length = 1;
        return UNICODE_BIGRAM_OK;
    }
    if (first >= 0xc2 && first <= 0xdf && remaining >= 2 &&
        input[1] >= 0x80 && input[1] <= 0xbf) {
        *code_point = ((uint32_t)(first & 0x1f) << 6) |
                      (uint32_t)(input[1] & 0x3f);
        *length = 2;
        return UNICODE_BIGRAM_OK;
    }
    if (first >= 0xe0 && first <= 0xef && remaining >= 3 &&
        input[1] >= 0x80 && input[1] <= 0xbf &&
        input[2] >= 0x80 && input[2] <= 0xbf &&
        !(first == 0xe0 && input[1] < 0xa0) &&
        !(first == 0xed && input[1] > 0x9f)) {
        *code_point = ((uint32_t)(first & 0x0f) << 12) |
                      ((uint32_t)(input[1] & 0x3f) << 6) |
                      (uint32_t)(input[2] & 0x3f);
        *length = 3;
        return UNICODE_BIGRAM_OK;
    }
    if (first >= 0xf0 && first <= 0xf4 && remaining >= 4 &&
        input[1] >= 0x80 && input[1] <= 0xbf &&
        input[2] >= 0x80 && input[2] <= 0xbf &&
        input[3] >= 0x80 && input[3] <= 0xbf &&
        !(first == 0xf0 && input[1] < 0x90) &&
        !(first == 0xf4 && input[1] > 0x8f)) {
        *code_point = ((uint32_t)(first & 0x07) << 18) |
                      ((uint32_t)(input[1] & 0x3f) << 12) |
                      ((uint32_t)(input[2] & 0x3f) << 6) |
                      (uint32_t)(input[3] & 0x3f);
        *length = 4;
        return UNICODE_BIGRAM_OK;
    }
    return UNICODE_BIGRAM_INVALID_UTF8;
}

static size_t encode_utf8(uint32_t code_point, unsigned char output[4]) {
    if (code_point <= 0x7f) {
        output[0] = (unsigned char)code_point;
        return 1;
    }
    if (code_point <= 0x7ff) {
        output[0] = (unsigned char)(0xc0 | (code_point >> 6));
        output[1] = (unsigned char)(0x80 | (code_point & 0x3f));
        return 2;
    }
    if (code_point <= 0xffff) {
        output[0] = (unsigned char)(0xe0 | (code_point >> 12));
        output[1] = (unsigned char)(0x80 | ((code_point >> 6) & 0x3f));
        output[2] = (unsigned char)(0x80 | (code_point & 0x3f));
        return 3;
    }
    output[0] = (unsigned char)(0xf0 | (code_point >> 18));
    output[1] = (unsigned char)(0x80 | ((code_point >> 12) & 0x3f));
    output[2] = (unsigned char)(0x80 | ((code_point >> 6) & 0x3f));
    output[3] = (unsigned char)(0x80 | (code_point & 0x3f));
    return 4;
}

int unicode_lowercase_bigram(
    const unsigned char *input,
    size_t input_bytes,
    unsigned char output[8],
    size_t *output_bytes
) {
    size_t input_offset = 0;
    size_t output_offset = 0;
    size_t count;

    if (input == NULL || output == NULL || output_bytes == NULL) {
        return UNICODE_BIGRAM_RANGE;
    }
    for (count = 0; count < 2; count += 1) {
        uint32_t code_point;
        size_t input_length;
        size_t output_length;
        int result = decode_utf8(
            input + input_offset,
            input_bytes - input_offset,
            &code_point,
            &input_length
        );
        if (result != UNICODE_BIGRAM_OK) {
            return result;
        }
        output_length = encode_utf8(
            lowercase_code_point(code_point),
            output + output_offset
        );
        input_offset += input_length;
        output_offset += output_length;
    }
    if (input_offset != input_bytes) {
        return UNICODE_BIGRAM_RANGE;
    }
    *output_bytes = output_offset;
    return UNICODE_BIGRAM_OK;
}
