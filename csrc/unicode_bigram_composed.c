#include "unicode_bigram_composed.h"

#include "unicode_norm.h"

typedef struct RemapContext {
    unicode_bigram_callback callback;
    void *context;
    const unsigned int *starts;
    const unsigned int *ends;
    size_t code_points;
} RemapContext;

/*
 * 正規化後のバッファ上の位置を原文の位置へ読み替える。position は bigram の先頭
 * コードポイント番号なので、対応表をそのまま引ける (バイト位置からの逆引きは要らない)。
 */
static int remap_token(
    void *context,
    const unsigned char *token,
    size_t token_bytes,
    size_t start,
    size_t end,
    size_t position
) {
    RemapContext *remap = context;

    if (position + 1 < remap->code_points) {
        start = remap->starts[position];
        end = remap->ends[position + 1];
    }
    return remap->callback(remap->context, token, token_bytes, start, end, position);
}

int unicode_bigram_tokenize_composed(
    const unsigned char *text,
    size_t text_bytes,
    unsigned char *scratch,
    size_t scratch_bytes,
    unsigned int *starts,
    unsigned int *ends,
    size_t map_capacity,
    unicode_bigram_callback callback,
    void *context
) {
    RemapContext remap;
    size_t normalized_bytes = 0;
    size_t code_points = 0;
    int result;

    if (unicode_norm_is_already_composed(text, text_bytes)) {
        return unicode_bigram_tokenize(text, text_bytes, callback, context);
    }
    if (scratch == NULL || starts == NULL || ends == NULL) {
        return UNICODE_NORM_RANGE;
    }
    result = unicode_norm_compose(
        text, text_bytes, scratch, scratch_bytes, &normalized_bytes,
        starts, ends, map_capacity, &code_points
    );
    if (result != UNICODE_NORM_OK) {
        return result;
    }
    remap.callback = callback;
    remap.context = context;
    remap.starts = starts;
    remap.ends = ends;
    remap.code_points = code_points;
    return unicode_bigram_tokenize(scratch, normalized_bytes, remap_token, &remap);
}
