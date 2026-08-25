#include "unicode_bigram.h"
#include "unicode_lower.h"

#include <assert.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct Capture {
    const char *tokens[16];
    size_t token_bytes[16];
    size_t starts[16];
    size_t ends[16];
    size_t count;
    size_t fail_at;
    int fail_code;
} Capture;

static int capture_token(
    void *context,
    const unsigned char *token,
    size_t token_bytes,
    size_t start,
    size_t end,
    size_t position
) {
    Capture *capture = context;
    assert(position == capture->count);
    if (capture->count == capture->fail_at) {
        return capture->fail_code;
    }
    assert(capture->count < 16);
    capture->tokens[capture->count] = (const char *)token;
    capture->token_bytes[capture->count] = token_bytes;
    capture->starts[capture->count] = start;
    capture->ends[capture->count] = end;
    capture->count += 1;
    return UNICODE_BIGRAM_OK;
}

static Capture tokenize(const unsigned char *text, size_t bytes) {
    Capture capture = {{0}, {0}, {0}, {0}, 0, (size_t)-1, 0};
    assert(unicode_bigram_tokenize(text, bytes, capture_token, &capture) == 0);
    return capture;
}

static void expect_token(const Capture *capture, size_t index, const char *token) {
    assert(index < capture->count);
    assert(capture->token_bytes[index] == strlen(token));
    assert(memcmp(capture->tokens[index], token, strlen(token)) == 0);
}

static void test_lengths_and_scripts(void) {
    Capture empty = tokenize((const unsigned char *)"", 0);
    Capture one = tokenize((const unsigned char *)"検", strlen("検"));
    Capture two = tokenize((const unsigned char *)"検索", strlen("検索"));
    Capture japanese = tokenize((const unsigned char *)"全文検索", strlen("全文検索"));
    Capture ascii = tokenize((const unsigned char *)"hello", 5);
    Capture kana = tokenize((const unsigned char *)"かなカナ", strlen("かなカナ"));

    assert(empty.count == 0);
    assert(one.count == 0);
    assert(two.count == 1);
    expect_token(&two, 0, "検索");
    assert(japanese.count == 3);
    expect_token(&japanese, 0, "全文");
    expect_token(&japanese, 1, "文検");
    expect_token(&japanese, 2, "検索");
    assert(japanese.starts[2] == strlen("全文"));
    assert(japanese.ends[2] == strlen("全文検索"));
    assert(ascii.count == 4);
    expect_token(&ascii, 0, "he");
    expect_token(&ascii, 3, "lo");
    assert(kana.count == 3);
}

static void test_code_point_boundaries(void) {
    const char *non_bmp = "A😀B";
    const char *combining = "e\xcc\x81x";
    const char *variation = "葛\xef\xb8\x80飾";
    const char *zwj = "👩‍💻";
    Capture capture = tokenize((const unsigned char *)non_bmp, strlen(non_bmp));

    assert(capture.count == 2);
    expect_token(&capture, 0, "A😀");
    expect_token(&capture, 1, "😀B");
    assert(tokenize((const unsigned char *)combining, strlen(combining)).count == 2);
    assert(tokenize((const unsigned char *)variation, strlen(variation)).count == 2);
    assert(tokenize((const unsigned char *)zwj, strlen(zwj)).count == 2);
}

static void test_embedded_nul(void) {
    const unsigned char text[] = {'a', 0, 'b'};
    Capture capture = tokenize(text, sizeof(text));
    assert(capture.count == 2);
    assert(capture.token_bytes[0] == 2);
    assert(capture.tokens[0][0] == 'a' && capture.tokens[0][1] == 0);
    assert(capture.starts[1] == 1 && capture.ends[1] == 3);
}

static void expect_invalid(const unsigned char *text, size_t bytes) {
    Capture capture = {{0}, {0}, {0}, {0}, 0, (size_t)-1, 0};
    assert(
        unicode_bigram_tokenize(text, bytes, capture_token, &capture) ==
        UNICODE_BIGRAM_INVALID_UTF8
    );
    assert(capture.count == 0);
}

static void test_invalid_utf8(void) {
    const unsigned char truncated[] = {0xe3, 0x81};
    const unsigned char overlong[] = {0xc0, 0xaf};
    const unsigned char surrogate[] = {0xed, 0xa0, 0x80};
    const unsigned char too_large[] = {0xf4, 0x90, 0x80, 0x80};
    const unsigned char stray[] = {0x80};

    expect_invalid(truncated, sizeof(truncated));
    expect_invalid(overlong, sizeof(overlong));
    expect_invalid(surrogate, sizeof(surrogate));
    expect_invalid(too_large, sizeof(too_large));
    expect_invalid(stray, sizeof(stray));
}

static void test_callback_error(void) {
    Capture capture = {{0}, {0}, {0}, {0}, 0, 1, 77};
    int result = unicode_bigram_tokenize(
        (const unsigned char *)"abcd",
        4,
        capture_token,
        &capture
    );
    assert(result == 77);
    assert(capture.count == 1);
}

static void expect_lowercase(const char *input, const char *expected) {
    unsigned char output[8];
    size_t output_bytes = 0;

    assert(
        unicode_lowercase_bigram(
            (const unsigned char *)input,
            strlen(input),
            output,
            &output_bytes
        ) == UNICODE_BIGRAM_OK
    );
    assert(output_bytes == strlen(expected));
    assert(memcmp(output, expected, output_bytes) == 0);
}

static void test_unicode_lowercase(void) {
    expect_lowercase("AZ", "az");
    expect_lowercase("ÄP", "äp");
    expect_lowercase("ΑΣ", "ασ");
    expect_lowercase("𐐀A", "𐐨a");
    expect_lowercase("全文", "全文");
}

static unsigned char hex_digit(char value) {
    if (value >= '0' && value <= '9') {
        return (unsigned char)(value - '0');
    }
    if (value >= 'a' && value <= 'f') {
        return (unsigned char)(value - 'a' + 10);
    }
    assert(0 && "invalid hexadecimal digit");
    return 0;
}

static size_t decode_hex(const char *hex, unsigned char *output, size_t capacity) {
    size_t length = strlen(hex);
    size_t index;

    assert(length % 2 == 0);
    assert(length / 2 <= capacity);
    for (index = 0; index < length; index += 2) {
        output[index / 2] = (unsigned char)(
            (hex_digit(hex[index]) << 4) | hex_digit(hex[index + 1])
        );
    }
    return length / 2;
}

static void test_shared_casefold_corpus(const char *path) {
    FILE *file = fopen(path, "rb");
    char line[2048];

    assert(file != NULL);
    while (fgets(line, sizeof(line), file) != NULL) {
        char *input_hex;
        char *expected_hex;
        unsigned char input[8];
        unsigned char expected[8];
        unsigned char actual[8];
        size_t input_bytes;
        size_t expected_bytes;
        size_t actual_bytes = 0;

        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }
        input_hex = strchr(line, '\t');
        assert(input_hex != NULL);
        *input_hex++ = '\0';
        expected_hex = strchr(input_hex, '\t');
        assert(expected_hex != NULL);
        *expected_hex++ = '\0';
        assert(strchr(expected_hex, '\t') == NULL);

        input_bytes = decode_hex(input_hex, input, sizeof(input));
        expected_bytes = decode_hex(expected_hex, expected, sizeof(expected));
        assert(
            unicode_lowercase_bigram(
                input,
                input_bytes,
                actual,
                &actual_bytes
            ) == UNICODE_BIGRAM_OK
        );
        assert(actual_bytes == expected_bytes);
        assert(memcmp(actual, expected, actual_bytes) == 0);
    }
    assert(ferror(file) == 0);
    assert(fclose(file) == 0);
}

static void test_shared_corpus(const char *path) {
    FILE *file = fopen(path, "rb");
    char line[2048];

    assert(file != NULL);
    while (fgets(line, sizeof(line), file) != NULL) {
        char *input_hex;
        char *expected_result;
        char *token_hexes;
        unsigned char input[512];
        size_t input_bytes;
        Capture capture = {{0}, {0}, {0}, {0}, 0, (size_t)-1, 0};
        int result;
        size_t expected_count = 0;

        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }
        input_hex = strchr(line, '\t');
        assert(input_hex != NULL);
        *input_hex++ = '\0';
        expected_result = strchr(input_hex, '\t');
        assert(expected_result != NULL);
        *expected_result++ = '\0';
        token_hexes = strchr(expected_result, '\t');
        if (token_hexes == NULL) {
            token_hexes = expected_result + strlen(expected_result);
        } else {
            *token_hexes++ = '\0';
            assert(strchr(token_hexes, '\t') == NULL);
        }

        input_bytes = decode_hex(input_hex, input, sizeof(input));
        result = unicode_bigram_tokenize(input, input_bytes, capture_token, &capture);
        if (strcmp(expected_result, "invalid_utf8") == 0) {
            assert(result == UNICODE_BIGRAM_INVALID_UTF8);
            assert(capture.count == 0);
            continue;
        }
        assert(strcmp(expected_result, "ok") == 0);
        assert(result == UNICODE_BIGRAM_OK);

        while (*token_hexes != '\0') {
            char *separator = strchr(token_hexes, ',');
            unsigned char expected[512];
            size_t expected_bytes;

            if (separator != NULL) {
                *separator = '\0';
            }
            expected_bytes = decode_hex(token_hexes, expected, sizeof(expected));
            assert(expected_count < capture.count);
            assert(capture.token_bytes[expected_count] == expected_bytes);
            assert(memcmp(capture.tokens[expected_count], expected, expected_bytes) == 0);
            expected_count += 1;
            if (separator == NULL) {
                break;
            }
            token_hexes = separator + 1;
        }
        assert(capture.count == expected_count);
    }
    assert(ferror(file) == 0);
    assert(fclose(file) == 0);
}

int main(int argument_count, char **arguments) {
    assert(argument_count == 3);
    test_lengths_and_scripts();
    test_code_point_boundaries();
    test_embedded_nul();
    test_invalid_utf8();
    test_callback_error();
    test_unicode_lowercase();
    test_shared_corpus(arguments[1]);
    test_shared_casefold_corpus(arguments[2]);
    return 0;
}
