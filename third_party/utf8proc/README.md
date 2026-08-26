# utf8proc

Vendored from https://github.com/JuliaStrings/utf8proc (v2.11.3, MIT).

正規化 (NFC) にだけ使う。分解済みの文字列は bigram の窓が結合文字を独立した単位として
切るため、窓を作る前に合成しておく必要がある (csrc/unicode_norm.c)。

更新するときは utf8proc.c / utf8proc.h / utf8proc_data.c / LICENSE.md を差し替え、
`NormalizationTest.txt` に対する適合性テストを回す。

大文字小文字の畳み込みには使っていない。C 側の表も Go 側の strings.ToLower も Go の
unicode 由来で、2 実装を揃えるために出所を分けない。表が Go から遅れていないことは
internal/generateunicode の test が見る。

utf8proc と Go は Unicode の版が違う (この版は 17.0.0、Go は 15.0.0)。正準分解の写像は
一度割り当てられたら変わらないという安定性ポリシーがあるので、新しい実装が古い
NormalizationTest.txt を通ることに問題はない。検証されないのは、テストの版より後に
追加された文字だけ。

正準分解しか使っていないので、大きさが問題になるなら削る余地がある。この表は互換分解
(NFKC)・大文字小文字・書記素分割ぶんも抱えており、拡張の .rodata の大半 (28KB → 379KB
の増分) がこれ。読み取り専用データなので常駐量は触ったページぶんに留まるが、配布サイズが
効く用途 (WASM 等) では正準分解だけの表に差し替える選択肢がある。
