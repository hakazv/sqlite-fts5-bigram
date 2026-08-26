#ifndef UNICODE_BIGRAM_COMPOSED_H
#define UNICODE_BIGRAM_COMPOSED_H

#include <stddef.h>

#include "unicode_bigram.h"

/*
 * 正規化してから bigram を切る。トークン化の入口はここ 1 つで、FTS5 の glue も
 * テストの harness も同じ経路を通る。
 *
 * scratch は正規化した本文の置き場で text_bytes 以上、starts / ends は出力コードポイント
 * ごとの原文バイト範囲で map_capacity は text_bytes + 1 以上あればよい (合成は文字数を
 * 増やさない)。既に正規化済みの本文では原文をそのまま走らせるので、どちらも触らない。
 *
 * callback が受け取る start / end は常に原文のバイト位置。
 */
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
);

#endif
