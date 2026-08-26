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
	"runtime"
	"strings"
	"unicode/utf8"
	"unsafe"

	fts5bigram "github.com/hakazv/sqlite-fts5-bigram"
	"modernc.org/libc"
	sqlite3 "modernc.org/sqlite/lib"
)

// TokenizerName is the name accepted by FTS5's tokenize option.
const TokenizerName = fts5bigram.TokenizerName

const (
	sqliteError  = int32(1)
	sqliteNoMem  = int32(7)
	sqliteMisuse = int32(21)
	sqliteRow    = int32(100)
)

var (
	fts5APIPointerType = "fts5_api_ptr\x00"
	fts5APIQuery       = "SELECT fts5(?1)\x00"
	tokenizerName      = TokenizerName + "\x00"
	tokenizer          = sqlite3.Tfts5_tokenizer_v2{
		FiVersion:  2,
		FxCreate:   functionPointer(createTokenizer),
		FxDelete:   functionPointer(deleteTokenizer),
		FxTokenize: functionPointer(tokenize),
	}
)

func init() {
	tls := libc.NewTLS()
	defer tls.Close()
	if result := sqlite3.Xsqlite3_auto_extension(tls, functionPointer(registerExtension)); result != 0 {
		panic("register unicode_bigram SQLite extension")
	}
}

func registerExtension(tls *libc.TLS, db, _ /* error message */, _ /* SQLite API */ uintptr) int32 {
	if db == 0 {
		return sqliteMisuse
	}
	// sqlite-fts5-bigram uses the locale-aware tokenizer v2 API added in SQLite 3.47.
	if sqlite3.Xsqlite3_libversion_number(tls) < 3_047_000 {
		return sqliteError
	}

	api, result := getFTS5API(tls, db)
	if result != 0 {
		return result
	}
	if api == nil || api.FiVersion < 3 || api.FxCreateTokenizer_v2 == 0 {
		return sqliteError
	}

	create := *(*func(*libc.TLS, uintptr, uintptr, uintptr, uintptr, uintptr) int32)(
		unsafe.Pointer(&struct{ pointer uintptr }{api.FxCreateTokenizer_v2}),
	)
	return create(
		tls,
		uintptr(unsafe.Pointer(api)),
		cStringPointer(tokenizerName),
		0,
		uintptr(unsafe.Pointer(&tokenizer)),
		0,
	)
}

func getFTS5API(tls *libc.TLS, db uintptr) (*sqlite3.Tfts5_api, int32) {
	pointerBytes := int(unsafe.Sizeof(uintptr(0)))
	frame := tls.Alloc(2 * pointerBytes)
	defer tls.Free(2 * pointerBytes)
	statementPointer := frame
	apiPointer := frame + uintptr(pointerBytes)

	result := sqlite3.Xsqlite3_prepare_v2(
		tls,
		db,
		cStringPointer(fts5APIQuery),
		-1,
		statementPointer,
		0,
	)
	if result != 0 {
		return nil, result
	}
	statement := **(**uintptr)(indirectPointer(statementPointer))

	result = sqlite3.Xsqlite3_bind_pointer(
		tls,
		statement,
		1,
		apiPointer,
		cStringPointer(fts5APIPointerType),
		0,
	)
	if result == 0 {
		result = sqlite3.Xsqlite3_step(tls, statement)
		if result == sqliteRow {
			result = 0
		}
	}
	finalizeResult := sqlite3.Xsqlite3_finalize(tls, statement)
	if result == 0 {
		result = finalizeResult
	}
	api := **(**uintptr)(indirectPointer(apiPointer))
	if result != 0 || api == 0 {
		if result == 0 {
			result = sqliteError
		}
		return nil, result
	}
	return *(**sqlite3.Tfts5_api)(indirectPointer(api)), 0
}

func createTokenizer(
	tls *libc.TLS,
	_ /* context */, arguments uintptr,
	argumentCount int32,
	output uintptr,
) int32 {
	if argumentCount < 0 || argumentCount%2 != 0 || output == 0 {
		return sqliteError
	}
	caseSensitive := byte(0)
	if argumentCount > 0 {
		if arguments == 0 {
			return sqliteError
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
				return sqliteError
			}
			switch libc.GoString(value) {
			case "0":
				caseSensitive = 0
			case "1":
				caseSensitive = 1
			default:
				return sqliteError
			}
		}
	}
	instance := sqlite3.Xsqlite3_malloc(tls, 1)
	if instance == 0 {
		return sqliteNoMem
	}
	libc.GoBytes(instance, 1)[0] = caseSensitive
	**(**uintptr)(indirectPointer(output)) = instance
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
		return sqliteError
	}
	if textBytes == 0 {
		return 0
	}

	text := libc.GoBytes(textPointer, int(textBytes))
	callback := *(*func(*libc.TLS, uintptr, int32, uintptr, int32, int32, int32) int32)(
		unsafe.Pointer(&struct{ pointer uintptr }{tokenCallback}),
	)
	caseSensitive := libc.GoBytes(tokenizer, 1)[0] != 0
	return walkBigrams(text, func(token []byte, start, end int) int32 {
		if !caseSensitive {
			token = lowercaseBigram(token)
		}
		result := callback(
			tls,
			context,
			0,
			uintptr(unsafe.Pointer(&token[0])),
			int32(len(token)),
			int32(start),
			int32(end),
		)
		runtime.KeepAlive(token)
		return result
	})
}

func lowercaseBigram(token []byte) []byte {
	lowered := strings.ToLower(unsafe.String(&token[0], len(token)))
	return []byte(lowered)
}

// walkBigrams は隣り合う 2 コードポイントを 1 トークンとして送る。
//
// token は正規化後の文字列なので、入力の部分列とは限らない。start / end は原文の
// バイト位置で、FTS5 の snippet / highlight がそれを使う。
func walkBigrams(text []byte, yield func(token []byte, start, end int) int32) int32 {
	if !utf8.Valid(text) {
		return sqliteError
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

func cStringPointer(value string) uintptr {
	return uintptr(unsafe.Pointer(unsafe.StringData(value)))
}

func functionPointer(function any) uintptr {
	type iface [2]uintptr
	return (*iface)(unsafe.Pointer(&function))[1]
}

// indirectPointer follows modernc's translated-C convention for dereferencing
// an emulated C address without converting that uintptr directly to a Go pointer.
func indirectPointer(address uintptr) unsafe.Pointer {
	return unsafe.Pointer(&address)
}
