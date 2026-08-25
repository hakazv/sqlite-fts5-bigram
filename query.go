// Package fts5bigram provides shared constants and query helpers for the
// unicode_bigram SQLite FTS5 tokenizer.
package fts5bigram

import (
	"errors"
	"strings"
	"unicode/utf8"
)

// TokenizerName is the name accepted by FTS5's tokenize option.
const TokenizerName = "unicode_bigram"

var (
	// ErrInvalidUTF8 indicates that a phrase is not valid UTF-8.
	ErrInvalidUTF8 = errors.New("FTS5 phrase must be valid UTF-8")
	// ErrPhraseTooShort indicates that a phrase cannot produce a bigram.
	ErrPhraseTooShort = errors.New("FTS5 phrase must contain at least two Unicode code points")
)

// PhraseQuery returns text as an escaped FTS5 phrase suitable for binding to a
// MATCH parameter.
func PhraseQuery(text string) (string, error) {
	if !utf8.ValidString(text) {
		return "", ErrInvalidUTF8
	}
	if utf8.RuneCountInString(text) < 2 {
		return "", ErrPhraseTooShort
	}
	return `"` + strings.ReplaceAll(text, `"`, `""`) + `"`, nil
}
