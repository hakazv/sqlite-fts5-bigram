#include "unicode_norm.h"

#include "utf8proc.h"

#include <stdint.h>

/*
 * 正規化そのものは utf8proc に任せ、ここは「どの入力バイトから出たか」を持ち回る。
 *
 * 分割の単位は書記素クラスタ。正準合成も正準順序も書記素の境界を越えないので、
 * クラスタごとに正規化した結果は全体を正規化した結果と一致する。境界の判定自体も
 * utf8proc に委ねている (どこで切れるかを自前で決めると、ハングルの字母や
 * インド系の母音記号のように「2 文字目も starter」の組を取りこぼす)。
 */

#define CLUSTER_CAPACITY 64

static utf8proc_ssize_t decode_one(
    const unsigned char *text,
    size_t remaining,
    utf8proc_int32_t *code_point
) {
    return utf8proc_iterate(
        (const utf8proc_uint8_t *)text,
        (utf8proc_ssize_t)remaining,
        code_point
    );
}

int unicode_norm_is_already_composed(const unsigned char *text, size_t text_bytes) {
    size_t offset = 0;

    if (text_bytes > 0 && text == NULL) {
        return 0;
    }
    while (offset < text_bytes) {
        utf8proc_int32_t code_point = 0;
        utf8proc_ssize_t length = decode_one(text + offset, text_bytes - offset, &code_point);
        const utf8proc_property_t *property;

        if (length <= 0) {
            return 0;
        }
        /*
         * ASCII は正規化で変わらないので、その場で抜ける (本文の大半はここ)。
         *
         * それ以外は「結合文字」「分解を持つ」に加えて「合成の 2 文字目になり得る」も
         * 見る必要がある。ハングルの字母やインド系の母音記号 (U+09BE 等) は結合文字でも
         * 分解持ちでもないのに、直前の starter と合成する。ここを落とすと、正規化が要る
         * 本文を「済み」と判定して素通しする。
         */
        if (code_point >= 0x80) {
            property = utf8proc_get_property(code_point);
            if (property->combining_class != 0 ||
                property->decomp_seqindex != UINT16_MAX ||
                property->comb_issecond) {
                return 0;
            }
            if (code_point >= 0x1100 && code_point <= 0x11ff) {
                return 0;
            }
        }
        offset += (size_t)length;
    }
    return 1;
}

int unicode_norm_compose(
    const unsigned char *text,
    size_t text_bytes,
    unsigned char *out,
    size_t out_capacity,
    size_t *out_bytes,
    unsigned int *starts,
    unsigned int *ends,
    size_t map_capacity,
    size_t *out_code_points
) {
    size_t cluster_length = 0;
    size_t cluster_start = 0;
    size_t offset = 0;
    utf8proc_int32_t previous = -1;
    utf8proc_int32_t break_state = 0;

    if ((text_bytes > 0 && text == NULL) || out == NULL || starts == NULL || ends == NULL ||
        out_bytes == NULL || out_code_points == NULL) {
        return UNICODE_NORM_RANGE;
    }
    *out_bytes = 0;
    *out_code_points = 0;

    for (;;) {
        utf8proc_int32_t code_point = 0;
        utf8proc_ssize_t length = 0;
        int at_end = offset == text_bytes;
        int starts_new_cluster;

        if (!at_end) {
            length = decode_one(text + offset, text_bytes - offset, &code_point);
            if (length <= 0) {
                return UNICODE_NORM_INVALID_UTF8;
            }
        }
        starts_new_cluster =
            at_end || (previous >= 0 &&
                       utf8proc_grapheme_break_stateful(previous, code_point, &break_state));

        if (starts_new_cluster && cluster_length > 0) {
            utf8proc_int32_t decomposed[CLUSTER_CAPACITY * 4];
            utf8proc_ssize_t decomposed_length;
            utf8proc_ssize_t normalized_length;

            /*
             * 正準順序 (結合クラスによる並べ替え) は buffer 版の decompose が行う。
             * 1 文字ずつの utf8proc_decompose_char には並べ替えが無く、
             * D + horn + dot-above + dot-below のような並びで合成先を取り違える。
             */
            decomposed_length = utf8proc_decompose(
                (const utf8proc_uint8_t *)(text + cluster_start),
                (utf8proc_ssize_t)(offset - cluster_start),
                decomposed,
                (utf8proc_ssize_t)(CLUSTER_CAPACITY * 4),
                UTF8PROC_STABLE | UTF8PROC_COMPOSE
            );
            if (decomposed_length < 0) {
                return UNICODE_NORM_RANGE;
            }
            normalized_length = utf8proc_normalize_utf32(
                decomposed, decomposed_length, UTF8PROC_STABLE | UTF8PROC_COMPOSE
            );
            if (normalized_length < 0) {
                return UNICODE_NORM_RANGE;
            }
            for (utf8proc_ssize_t index = 0; index < normalized_length; index++) {
                utf8proc_uint8_t encoded[4];
                utf8proc_ssize_t encoded_bytes =
                    utf8proc_encode_char(decomposed[index], encoded);
                if (encoded_bytes <= 0) {
                    return UNICODE_NORM_RANGE;
                }
                if (*out_bytes + (size_t)encoded_bytes > out_capacity ||
                    *out_code_points >= map_capacity) {
                    return UNICODE_NORM_RANGE;
                }
                for (utf8proc_ssize_t byte = 0; byte < encoded_bytes; byte++) {
                    out[*out_bytes + (size_t)byte] = (unsigned char)encoded[byte];
                }
                /* クラスタから出た文字は、そのクラスタ全体を指す。 */
                starts[*out_code_points] = (unsigned int)cluster_start;
                ends[*out_code_points] = (unsigned int)offset;
                *out_bytes += (size_t)encoded_bytes;
                *out_code_points += 1;
            }
            cluster_length = 0;
        }
        if (at_end) {
            return UNICODE_NORM_OK;
        }
        if (cluster_length == 0) {
            cluster_start = offset;
        }
        if (cluster_length >= CLUSTER_CAPACITY) {
            return UNICODE_NORM_RANGE;
        }
        cluster_length += 1;
        previous = code_point;
        offset += (size_t)length;
    }
}

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

int unicode_norm_tokenize(
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
