# sqlite-fts5-bigram

`sqlite-fts5-bigram` adds a Unicode bigram tokenizer named `unicode_bigram`
to SQLite FTS5. It supports indexed substring search for text without reliable
word boundaries, including Japanese.

```text
input:  全文検索
tokens: 全文 / 文検 / 検索
```

Requirements:

- SQLite 3.47.0 or newer with FTS5
- Valid UTF-8 input

The tokenizer uses adjacent Unicode code points and is case-insensitive by
default. Text is composed (NFC) before it is split, so the same word matches
whichever normalization form it is written in; diacritics are preserved, not
removed. Inputs shorter than two code points after composing emit no tokens.

Use `case_sensitive 1` for exact case matching:

```sql
tokenize = 'unicode_bigram case_sensitive 1'
```

Use quoted FTS5 phrases to preserve bigram adjacency:

```sql
CREATE VIRTUAL TABLE passages USING fts5(
  text,
  tokenize = 'unicode_bigram'
);

INSERT INTO passages(text) VALUES ('全文検索を使う');

SELECT rowid, text
FROM passages
WHERE passages MATCH 'text : "全文検索"';
```

Bind application input with `PhraseQuery` (Go) or `phrase_query` (Rust) to
escape an FTS5 phrase and reject input too short to produce a bigram.

## Unicode normalization

Text is composed (NFC) before it is split into bigrams, on both the indexing and
the query side. A dakuten can be written as one character or as a base plus a
combining mark; without composing first, the combining mark becomes a token unit
of its own, so 「イド」 matches a search for 「イト」 and a composed query for
「ガイ」 matches nothing. Files synced from macOS carry decomposed names and text.

Composition is not optional and is unaffected by `case_sensitive`: the two
settle different questions, whether a character is the same one and whether a
letter is the same letter.

Byte offsets passed to `xToken` keep pointing into the original text, so
`snippet()` and `highlight()` still land on the source.

Conformance is checked against the Unicode Consortium's `NormalizationTest.txt`
(`third_party/unicode/`) from both implementations — the C tokenizer, which uses
utf8proc, and the pure-Go modernc driver, which uses golang.org/x/text. Two
implementations of one tokenizer agree because both are held to the
specification, not to each other.

## Go with modernc.org/sqlite

The Go adapter is CGO-free and registers the tokenizer on every new
`modernc.org/sqlite` connection.

```sh
go get github.com/hakazv/sqlite-fts5-bigram/driver/modernc@v0.3.2
```

Import the adapter for its side effect before opening connections:

```go
import (
    "database/sql"

    fts5bigram "github.com/hakazv/sqlite-fts5-bigram"
    _ "github.com/hakazv/sqlite-fts5-bigram/driver/modernc"
    _ "modernc.org/sqlite"
)

db, err := sql.Open("sqlite", "search.db")
query, err := fts5bigram.PhraseQuery("全文検索")
```

See the [Go integration test](driver/modernc/register_test.go) for table setup,
parameter binding, and querying.

## Rust

Add the Git dependency:

```toml
[dependencies]
rusqlite = { version = "0.40.2", features = ["bundled"] }
sqlite-fts5-bigram = { git = "https://github.com/hakazv/sqlite-fts5-bigram.git", tag = "v0.3.2" }
```

SSH is also supported: `ssh://git@github.com/hakazv/sqlite-fts5-bigram.git`.

Register the tokenizer once per connection before using the FTS5 table:

```rust
use rusqlite::Connection;

let connection = Connection::open("search.db")?;

// SAFETY: handle() is a live sqlite3* used exclusively during registration.
unsafe {
    sqlite_fts5_bigram::register(connection.handle().cast())?;
}

let query = sqlite_fts5_bigram::phrase_query("全文検索")?;
```

See the [Rust integration test](tests/sqlite.rs) for registration, table setup,
parameter binding, and querying.

## C and C++

Build and link `fts5_bigram_static` with the application's SQLite library:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
cmake --install build --prefix install
```

Installed projects can use `find_package(sqlite_fts5_bigram CONFIG REQUIRED)`
and link `sqlite_fts5_bigram::static` together with SQLite.

```c
#include <sqlite3.h>
#include "sqlite3_fts5_bigram.h"

char *message = NULL;
int result = sqlite3_fts5bigram_register(db, &message);
if (result != SQLITE_OK) {
    sqlite3_free(message);
    return result;
}
```

See the installed-package consumer tests for [C](tests/cmake-package/consumer.c)
and [C++](tests/cmake-package/consumer.cpp).

Registration is connection-local. The caller releases a non-NULL error message
with `sqlite3_free()`.

## Loadable extension

The CMake build also creates `fts5_bigram.so`, `fts5_bigram.dylib`, or
`fts5_bigram.dll`. Load it before using a table configured with
`unicode_bigram`:

```text
.load ./build/fts5_bigram
```

Applications should enable extension loading only for the load operation.
Tagged GitHub Releases include x86-64 `.so` and `.dll`, a universal macOS
`.dylib`, and SHA-256 checksums.

## Development

```sh
just check      # everything CI runs
just generate   # rebuild the Unicode table the C tokenizer compiles in
```

Three toolchains build this, and a change to one can break another without
saying so, so there is one command that runs them all: the Rust checks, the C
core with its conformance tests, the Go adapter, and an install of the CMake
package with a consumer built against it. `just --list` shows the pieces if you
want to run one of them.

`just` is a convenience, not a requirement:

```sh
cargo fmt --check
cargo clippy --all-targets -- -D warnings
cargo test --all-targets
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
gofmt -l .
go vet ./...
go test ./...
```

`just generate` is needed when Go raises its Unicode version, because the table
the C side compiles in is generated from it. A test says so when that happens.
