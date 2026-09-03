// Package modernc registers sqlite-fts5-bigram's unicode_bigram tokenizer
// with modernc.org/sqlite.
//
// Import this package for its side effect before opening SQLite connections:
//
//	import (
//		"database/sql"
//		_ "github.com/hakazv/sqlite-fts5-bigram/driver/modernc"
//		_ "modernc.org/sqlite"
//	)
//
//	db, err := sql.Open("sqlite", "search.db")
//	// unicode_bigram is now available to every connection opened in-process.
//
// The adapter uses modernc's translated SQLite ABI and does not require CGO or
// a separately distributed loadable-extension file.
package modernc

import (
	"strings"
	"unicode/utf8"
	"unsafe"

	fts5bigram "github.com/hakazv/sqlite-fts5-bigram"
	"github.com/hakazv/sqlite-fts5-bigram/fts5modernc"
	"modernc.org/libc"
	sqlite3 "modernc.org/sqlite/lib"
)

// TokenizerName is the name accepted by FTS5's tokenize option.
const TokenizerName = fts5bigram.TokenizerName

var tokenizer = sqlite3.Tfts5_tokenizer_v2{
	FiVersion:  2,
	FxCreate:   fts5modernc.FunctionPointer(createTokenizer),
	FxDelete:   fts5modernc.FunctionPointer(deleteTokenizer),
	FxTokenize: fts5modernc.FunctionPointer(tokenize),
}

func init() {
	if err := fts5modernc.RegisterTokenizer(TokenizerName, &tokenizer); err != nil {
		panic(err)
	}
}

func createTokenizer(
	tls *libc.TLS,
	_ /* context */, arguments uintptr,
	argumentCount int32,
	output uintptr,
) int32 {
	if argumentCount < 0 || argumentCount%2 != 0 || output == 0 {
		return fts5modernc.ResultError
	}
	caseSensitive := byte(0)
	if argumentCount > 0 {
		if arguments == 0 {
			return fts5modernc.ResultError
		}
		pointerBytes := int(unsafe.Sizeof(uintptr(0)))
		argumentBytes := libc.GoBytes(arguments, int(argumentCount)*pointerBytes)
		argumentAt := func(index int) uintptr {
			return *(*uintptr)(unsafe.Pointer(&argumentBytes[index*pointerBytes]))
		}
		for index := 0; index < int(argumentCount); index += 2 {
			name := argumentAt(index)
			value := argumentAt(index + 1)
			if name == 0 || value == 0 || libc.GoString(name) != "case_sensitive" {
				return fts5modernc.ResultError
			}
			switch libc.GoString(value) {
			case "0":
				caseSensitive = 0
			case "1":
				caseSensitive = 1
			default:
				return fts5modernc.ResultError
			}
		}
	}
	instance := sqlite3.Xsqlite3_malloc(tls, 1)
	if instance == 0 {
		return fts5modernc.ResultNoMem
	}
	libc.GoBytes(instance, 1)[0] = caseSensitive
	**(**uintptr)(fts5modernc.IndirectPointer(output)) = instance
	return 0
}

func deleteTokenizer(tls *libc.TLS, tokenizer uintptr) {
	sqlite3.Xsqlite3_free(tls, tokenizer)
}

func tokenize(
	tls *libc.TLS,
	tokenizer, context uintptr,
	_ /* flags */ int32,
	textPointer uintptr,
	textBytes int32,
	_ /* locale */ uintptr,
	_ /* locale bytes */ int32,
	tokenCallback uintptr,
) int32 {
	if tokenizer == 0 || textBytes < 0 || tokenCallback == 0 || (textPointer == 0 && textBytes != 0) {
		return fts5modernc.ResultError
	}
	if textBytes == 0 {
		return 0
	}

	text := libc.GoBytes(textPointer, int(textBytes))
	callback := fts5modernc.TokenCallback(tokenCallback)
	caseSensitive := libc.GoBytes(tokenizer, 1)[0] != 0

	// FTS5 は受け取った語をそのまま memcpy でハッシュへ写す。エミュレートされた C から
	// 見て正当な確保でないと駄目なので、Go の側のメモリを直接渡さず C 側の作業領域へ写す。
	var scratch fts5modernc.TokenBuffer
	defer scratch.Free(tls)

	return walkBigrams(text, func(token []byte, start, end int) int32 {
		// 文字列のまま扱う。strings.ToLower は変換が要らなければ入力をそのまま返すので、
		// 大文字小文字を持たない文字 (日本語など) ではトークンごとの確保が起きない。
		text := unsafe.String(&token[0], len(token))
		if !caseSensitive {
			text = lowercaseBigram(text)
		}
		tokenPointer := scratch.Store(tls, text)
		if tokenPointer == 0 {
			return fts5modernc.ResultNoMem
		}
		return callback(
			tls,
			context,
			0,
			tokenPointer,
			int32(len(text)),
			int32(start),
			int32(end),
		)
	})
}

func lowercaseBigram(token string) string {
	return strings.ToLower(token)
}

// walkBigrams は隣り合う 2 コードポイントを 1 トークンとして送る。
//
// token は正規化後の文字列なので、入力の部分列とは限らない。start / end は原文の
// バイト位置で、FTS5 の snippet / highlight がそれを使う。
func walkBigrams(text []byte, yield func(token []byte, start, end int) int32) int32 {
	if !utf8.Valid(text) {
		return fts5modernc.ResultError
	}
	if len(text) == 0 {
		return 0
	}
	normalized := composeForTokenizing(text)
	content := normalized.text
	if len(content) == 0 {
		return 0
	}

	firstStart := 0
	_, firstBytes := utf8.DecodeRune(content)
	nextStart := firstBytes
	for index := 0; nextStart < len(content); index++ {
		_, secondBytes := utf8.DecodeRune(content[nextStart:])
		end := nextStart + secondBytes
		tokenStart, _ := normalized.codePointRange(index, firstStart, firstStart)
		_, tokenEnd := normalized.codePointRange(index+1, end, end)
		result := yield(content[firstStart:end], tokenStart, tokenEnd)
		if result != 0 {
			return result
		}
		firstStart = nextStart
		nextStart = end
	}
	return 0
}
