package modernc

import (
	"unicode/utf8"

	"golang.org/x/text/unicode/norm"
)

// composed は正規化した本文と、出力コードポイントごとの入力側のバイト範囲。
//
// 正規化はトークンを切る前に必要になる。分解済みの「ガ」は「カ + 結合濁点」の
// 2 コードポイントなので、コードポイントの窓は結合文字を独立した単位として切る。
// その結果「イド」が「イト」で引け、合成済みの「ガイ」では引けない、という
// 取りこぼしと誤ヒットが同時に起きる。
//
// オフセットを持ち回るのは、FTS5 の xToken へ渡す位置が原文を指し続ける必要が
// あるため (snippet / highlight が使う)。正規化後のバッファ上の位置では別の場所を指す。
type composed struct {
	text   []byte
	starts []int
	ends   []int
}

// composeForTokenizing は text を NFC へ正規化する。
//
// 範囲は正規化の境界 (starter とそれに続く結合文字) 単位で対応付ける。既に正規化済みの
// 本文では境界が 1 コードポイントずつなので原文の位置と完全に一致し、分解済みの箇所だけが
// その並び全体を指す。
func composeForTokenizing(text []byte) composed {
	if norm.NFC.IsNormal(text) {
		// 大半の本文はここで終わる。複製も確保もしない。
		return composed{text: text}
	}

	result := composed{
		text:   make([]byte, 0, len(text)),
		starts: make([]int, 0, utf8.RuneCount(text)),
		ends:   make([]int, 0, utf8.RuneCount(text)),
	}
	for offset := 0; offset < len(text); {
		boundary := norm.NFC.NextBoundary(text[offset:], true)
		if boundary <= 0 {
			boundary = len(text) - offset
		}
		segment := text[offset : offset+boundary]
		start := len(result.text)
		result.text = norm.NFC.Append(result.text, segment...)
		for position := start; position < len(result.text); {
			_, size := utf8.DecodeRune(result.text[position:])
			result.starts = append(result.starts, offset)
			result.ends = append(result.ends, offset+boundary)
			position += size
		}
		offset += boundary
	}
	return result
}

// codePointRange は正規化後の i 番目のコードポイントに対応する原文のバイト範囲。
// 正規化が不要だった本文では対応表を持たないので、正規化後の位置がそのまま原文の位置。
func (c composed) codePointRange(index, fallbackStart, fallbackEnd int) (int, int) {
	if c.starts == nil {
		return fallbackStart, fallbackEnd
	}
	if index < 0 || index >= len(c.starts) {
		return fallbackStart, fallbackEnd
	}
	return c.starts[index], c.ends[index]
}
