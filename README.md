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

The tokenizer uses adjacent Unicode code points exactly as provided. It does
not normalize or fold case, and inputs shorter than two code points emit no
tokens. Normalize indexed text and queries in the application when needed.

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

## Go with modernc.org/sqlite

The Go adapter is CGO-free and registers the tokenizer on every new
`modernc.org/sqlite` connection.

```sh
go get github.com/hakazv/sqlite-fts5-bigram/driver/modernc@v0.1.0
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

## Rust

Add the Git dependency:

```toml
[dependencies]
rusqlite = { version = "0.40.2", features = ["bundled"] }
sqlite-fts5-bigram = { git = "https://github.com/hakazv/sqlite-fts5-bigram.git", tag = "v0.1.0" }
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
go vet ./...
go test ./...
cargo fmt --check
cargo clippy --all-targets -- -D warnings
cargo test --all-targets
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```
