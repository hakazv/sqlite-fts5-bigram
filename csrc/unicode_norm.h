#ifndef UNICODE_NORM_H
#define UNICODE_NORM_H

#include <stddef.h>

/*
 * Canonical composition (NFC) for the bigram tokenizer.
 *
 * 正規化はトークンを切る前に必要になる。分解済みの「ガ」は「カ + 結合濁点」の
 * 2 コードポイントなので、コードポイントの窓は結合文字を独立した単位として切る。
 * その結果「イド」が「イト」で引け、合成済みの「ガイ」では引けない、という取りこぼしと
 * 誤ヒットが同時に起きる。
 *
 * 出力するコードポイントごとに、それを作った入力側のバイト範囲を返す。FTS5 の
 * xToken へ渡すオフセットは原文を指し続ける必要があり (snippet/highlight が使う)、
 * 正規化後のバッファ上の位置では別の場所を指してしまう。
 */

enum unicode_norm_result {
    UNICODE_NORM_OK = 0,
    UNICODE_NORM_INVALID_UTF8 = -1,
    UNICODE_NORM_RANGE = -2
};

/*
 * 正規化が要らない入力かを判定する。結合文字も分解を持つ文字も含まなければ、
 * 入力は既に NFC なので、呼び出し側は原文をそのまま使える (確保も複製も要らない)。
 * 判定は保守的で、偽であれば必ず正規化が要るが、真の側は取りこぼしても正しさは保たれる。
 */
int unicode_norm_is_already_composed(const unsigned char *text, size_t text_bytes);

/*
 * text を NFC へ正規化する。
 *
 * out には正規化後の UTF-8 を、starts/ends には出力コードポイントごとの入力側の
 * バイト範囲を書く。いずれも容量が足りなければ UNICODE_NORM_RANGE を返す。
 * 必要な容量は入力のバイト数と入力のコードポイント数を超えない (合成は文字数を
 * 減らすだけで増やさない)。
 */
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
);

#endif
