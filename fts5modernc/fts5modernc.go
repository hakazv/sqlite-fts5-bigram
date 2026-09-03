// Package fts5modernc registers FTS5 tokenizers with modernc.org/sqlite.
//
// modernc.org/sqlite runs translated C on an emulated libc, so it cannot load a
// native loadable extension (.so/.dylib). Handing SQLite Go function pointers
// through its translated ABI, as this package does, is the only way to add a
// tokenizer while staying pure Go.
//
// The package has no side effects of its own. Importing it registers nothing;
// call RegisterTokenizer from an init function in the package that owns the
// tokenizer.
//
// This is plumbing shared by tokenizer implementations. It exposes
// modernc.org/sqlite/lib and modernc.org/libc types because that is the ABI it
// adapts to, and it is therefore bound to those packages' compatibility.
package fts5modernc

import (
	"errors"
	"sync"
	"unsafe"

	"modernc.org/libc"
	sqlite3 "modernc.org/sqlite/lib"
)

// SQLite result codes a tokenizer implementation returns. Anything else it
// needs is in modernc.org/sqlite/lib; these are here because every tokenizer
// uses them.
const (
	ResultError = int32(1)
	ResultNoMem = int32(7)
)

const (
	resultMisuse = int32(21)
	resultRow    = int32(100)
)

// minimumLibVersion is SQLite 3.47, which added the locale-aware tokenizer v2
// API that RegisterTokenizer installs.
const minimumLibVersion = 3_047_000

var (
	fts5APIPointerType = "fts5_api_ptr\x00"
	fts5APIQuery       = "SELECT fts5(?1)\x00"
)

// registration is one tokenizer waiting to be installed on new connections.
//
// The name lives in emulated C memory rather than in a Go string. SQLite runs
// strlen over it, and strlen reads a machine word at a time, which walks past
// the end of a Go string allocated to exactly len(name)+1 bytes — checkptr
// fails the process for it under -race.
type registration struct {
	namePointer uintptr
	tokenizer   *sqlite3.Tfts5_tokenizer_v2
}

var (
	registryMu sync.Mutex
	registry   []registration
	hookAdded  bool
)

// RegisterTokenizer arranges for name to be installed as an FTS5 v2 tokenizer
// on every SQLite connection opened in this process afterwards.
//
// tokenizer supplies the xCreate, xDelete and xTokenize function pointers; how
// a tokenizer instance carries its per-table configuration is left to the
// caller, since implementations differ in what they need to store. Those
// pointers must come from top-level functions: FunctionPointer takes the code
// address out of a func value, and a closure's is not one translated C can call.
//
// It is meant to be called from an init function. It returns an error only when
// SQLite refuses the auto-extension hook, which is a programming error rather
// than a runtime condition.
func RegisterTokenizer(name string, tokenizer *sqlite3.Tfts5_tokenizer_v2) error {
	if name == "" || tokenizer == nil {
		return errors.New("fts5modernc: name and tokenizer are required")
	}

	tls := libc.NewTLS()
	defer tls.Close()

	namePointer := allocCString(tls, name)
	if namePointer == 0 {
		return errors.New("fts5modernc: out of memory naming " + name)
	}

	registryMu.Lock()
	registry = append(registry, registration{namePointer: namePointer, tokenizer: tokenizer})
	addHook := !hookAdded
	hookAdded = true
	registryMu.Unlock()

	// One auto-extension installs every registered tokenizer. Registering a hook
	// per tokenizer would work too, but SQLite's auto-extension list is a fixed
	// size, so it is not something to spend an entry on per caller.
	if addHook {
		if result := sqlite3.Xsqlite3_auto_extension(tls, FunctionPointer(installTokenizers)); result != 0 {
			return errors.New("fts5modernc: could not add the SQLite auto-extension hook")
		}
	}
	return nil
}

// installTokenizers runs on every new connection. It must be a top-level
// function: SQLite is handed its code address, which a closure does not have in
// a form translated C can call.
func installTokenizers(tls *libc.TLS, db, _ /* error message */, _ /* SQLite API */ uintptr) int32 {
	if db == 0 {
		return resultMisuse
	}
	if sqlite3.Xsqlite3_libversion_number(tls) < minimumLibVersion {
		return ResultError
	}

	api, result := fts5API(tls, db)
	if result != 0 {
		return result
	}
	if api == nil || api.FiVersion < 3 || api.FxCreateTokenizer_v2 == 0 {
		return ResultError
	}
	create := *(*func(*libc.TLS, uintptr, uintptr, uintptr, uintptr, uintptr) int32)(
		unsafe.Pointer(&struct{ pointer uintptr }{api.FxCreateTokenizer_v2}),
	)

	registryMu.Lock()
	pending := make([]registration, len(registry))
	copy(pending, registry)
	registryMu.Unlock()

	for _, entry := range pending {
		result := create(
			tls,
			uintptr(unsafe.Pointer(api)),
			entry.namePointer,
			0,
			uintptr(unsafe.Pointer(entry.tokenizer)),
			0,
		)
		if result != 0 {
			return result
		}
	}
	return 0
}

// allocCString copies value into emulated C memory as a NUL-terminated string.
// The block is deliberately never freed: a registration lasts for the life of
// the process, and SQLite reads the name on every connection that is opened.
func allocCString(tls *libc.TLS, value string) uintptr {
	size := len(value) + 1
	pointer := sqlite3.Xsqlite3_malloc(tls, int32(size))
	if pointer == 0 {
		return 0
	}
	buffer := libc.GoBytes(pointer, size)
	copy(buffer, value)
	buffer[len(value)] = 0
	return pointer
}

// fts5API fetches the fts5_api pointer for a connection, the way the FTS5
// documentation prescribes: prepare "SELECT fts5(?1)" and bind a pointer.
func fts5API(tls *libc.TLS, db uintptr) (*sqlite3.Tfts5_api, int32) {
	pointerBytes := int(unsafe.Sizeof(uintptr(0)))
	frame := tls.Alloc(2 * pointerBytes)
	defer tls.Free(2 * pointerBytes)
	statementPointer := frame
	apiPointer := frame + uintptr(pointerBytes)

	result := sqlite3.Xsqlite3_prepare_v2(tls, db, CStringPointer(fts5APIQuery), -1, statementPointer, 0)
	if result != 0 {
		return nil, result
	}
	statement := **(**uintptr)(IndirectPointer(statementPointer))

	result = sqlite3.Xsqlite3_bind_pointer(tls, statement, 1, apiPointer, CStringPointer(fts5APIPointerType), 0)
	if result == 0 {
		result = sqlite3.Xsqlite3_step(tls, statement)
		if result == resultRow {
			result = 0
		}
	}
	finalizeResult := sqlite3.Xsqlite3_finalize(tls, statement)
	if result == 0 {
		result = finalizeResult
	}
	api := **(**uintptr)(IndirectPointer(apiPointer))
	if result != 0 || api == 0 {
		if result == 0 {
			result = ResultError
		}
		return nil, result
	}
	return *(**sqlite3.Tfts5_api)(IndirectPointer(api)), 0
}

// TokenBuffer is the scratch space a tokenizer hands tokens to FTS5 through.
//
// FTS5 memcpys the token it is given straight into its hash, so the address has
// to be a real allocation as seen from the emulated C side; a Go string cannot
// be passed directly. The buffer grows to the longest token instead of being
// allocated per token, so one malloc does not run per token in a document.
type TokenBuffer struct {
	pointer uintptr
	size    int
}

// Store copies token into the scratch space and returns its C address, or 0 if
// the allocation failed.
//
// FTS5 only reads the token for the duration of the callback (it copies it into
// the index), so the next token may overwrite the same space. SQLite's own
// tokenizers reuse a single buffer the same way.
func (b *TokenBuffer) Store(tls *libc.TLS, token string) uintptr {
	// sqlite3_malloc(0) returns NULL, which would make an empty token
	// indistinguishable from a failed allocation. Always keep at least a byte.
	need := max(len(token), 1)
	if b.size < need {
		if b.pointer != 0 {
			sqlite3.Xsqlite3_free(tls, b.pointer)
		}
		b.pointer = sqlite3.Xsqlite3_malloc(tls, int32(need))
		if b.pointer == 0 {
			b.size = 0
			return 0
		}
		b.size = need
	}
	// Only this token is copied. Trailing bytes of a previous, longer token stay
	// behind it, but the length is passed separately so they are never read.
	copy(libc.GoBytes(b.pointer, len(token)), token)
	return b.pointer
}

// Free releases the scratch space. Calling it twice is safe.
func (b *TokenBuffer) Free(tls *libc.TLS) {
	if b.pointer != 0 {
		sqlite3.Xsqlite3_free(tls, b.pointer)
		b.pointer = 0
		b.size = 0
	}
}

// TokenCallback converts the xToken function pointer FTS5 passes to xTokenize
// into something callable.
func TokenCallback(pointer uintptr) func(tls *libc.TLS, context uintptr, flags int32, token uintptr, tokenBytes, start, end int32) int32 {
	return *(*func(*libc.TLS, uintptr, int32, uintptr, int32, int32, int32) int32)(
		unsafe.Pointer(&struct{ pointer uintptr }{pointer}),
	)
}

// CStringPointer returns the address of value's bytes. The string must stay
// reachable for as long as SQLite may read it, and must be NUL-terminated if
// SQLite treats it as a C string.
func CStringPointer(value string) uintptr {
	return uintptr(unsafe.Pointer(unsafe.StringData(value)))
}

// FunctionPointer extracts the code pointer from a Go func value so it can be
// handed to translated C.
func FunctionPointer(function any) uintptr {
	type iface [2]uintptr
	return (*iface)(unsafe.Pointer(&function))[1]
}

// IndirectPointer follows modernc's translated-C convention for dereferencing
// an emulated C address without converting that uintptr directly to a Go pointer.
func IndirectPointer(address uintptr) unsafe.Pointer {
	return unsafe.Pointer(&address)
}
