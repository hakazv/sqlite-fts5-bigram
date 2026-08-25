#include "unicode_bigram.h"

#include <assert.h>
#include <stddef.h>
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

int main(void) {
    test_lengths_and_scripts();
    test_code_point_boundaries();
    test_embedded_nul();
    test_invalid_utf8();
    test_callback_error();
    return 0;
}

