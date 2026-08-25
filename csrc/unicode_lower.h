#ifndef UNICODE_LOWER_H
#define UNICODE_LOWER_H

#include <stddef.h>

int unicode_lowercase_bigram(
    const unsigned char *input,
    size_t input_bytes,
    unsigned char output[8],
    size_t *output_bytes
);

#endif
