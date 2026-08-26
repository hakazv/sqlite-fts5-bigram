# utf8proc

Vendored from https://github.com/JuliaStrings/utf8proc (v2.11.3, MIT).

正規化 (NFC) にだけ使う。分解済みの文字列は bigram の窓が結合文字を独立した単位として
切るため、窓を作る前に合成しておく必要がある (csrc/unicode_norm.c)。

更新するときは utf8proc.c / utf8proc.h / utf8proc_data.c / LICENSE.md を差し替え、
`NormalizationTest.txt` に対する適合性テストを回す。

大文字小文字の畳み込みには使っていない。既存の生成表 (csrc/unicode_lower_table.inc) が
Go 実装の strings.ToLower と同じ Go の unicode 由来であり、2 実装を揃えるために
出所を分けない。
