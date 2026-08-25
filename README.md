# sqlite-fts5-bigram

`sqlite-fts5-bigram` adds the `unicode_bigram` tokenizer to SQLite FTS5. It is
intended for indexed substring search in languages such as Japanese where
words are not always separated by spaces.

```text
input:  全文検索
tokens: 全文 / 文検 / 検索
```

Choose one integration path:

| Consumer | Recommended path |
| --- | --- |
| Rust application that owns SQLite | Cargo dependency and static registration |
| C/C++ application that owns SQLite | CMake static library |
| SQLite CLI, Python, or another dynamic client | Loadable extension |

All paths register the same tokenizer and produce the same tokens.

## Requirements and behavior

- SQLite 3.47.0 or newer, built with FTS5
- UTF-8 input; invalid UTF-8 returns `SQLITE_ERROR`
- Adjacent Unicode code points are tokenized, including ASCII, combining marks,
  variation selectors, and ZWJ
- No normalization, case folding, whitespace removal, or punctuation removal
- No tokenizer arguments; `tokenize='unicode_bigram unexpected'` is rejected
- Empty and one-code-point inputs emit no tokens

Normalize indexed text and queries identically in the application when the
search requires normalization. Use a quoted FTS5 phrase for queries longer
than two code points so consecutive bigrams must remain adjacent.

## Rust: static registration

The Cargo crate compiles the C sources with `SQLITE_CORE`. It does not link its
own SQLite, so registration uses the SQLite connection already owned by the
application.

Add the public Git dependency and pin a reviewed commit. Both HTTPS and SSH
transports are supported; the example uses HTTPS because it works without
GitHub authentication and is the usual choice for a public dependency:

```toml
[dependencies]
rusqlite = { version = "0.40.2", features = ["bundled"] }
sqlite-fts5-bigram = {
  git = "https://github.com/hakazv/sqlite-fts5-bigram.git",
  rev = "8805349f77d379267840d4e4d86dc0cb63350e0e"
}
```

Available Git URLs:

- HTTPS: `https://github.com/hakazv/sqlite-fts5-bigram.git`
- SSH: `ssh://git@github.com/hakazv/sqlite-fts5-bigram.git`

Use the SSH URL when the consuming project intentionally standardizes on SSH;
GitHub SSH authentication must then be available locally and in CI.

Register the tokenizer once on every connection, before creating or accessing
an FTS5 table that names it:

```rust
use rusqlite::Connection;

fn open_search_database() -> Result<Connection, Box<dyn std::error::Error>> {
    let connection = Connection::open("search.db")?;

    // SAFETY: handle() is a live sqlite3* and registration completes before
    // the connection is shared or used by FTS5.
    unsafe {
        sqlite_fts5_bigram::register(connection.handle().cast())?;
    }

    connection.execute_batch(
        "CREATE VIRTUAL TABLE IF NOT EXISTS passages_fts USING fts5(
             text,
             tokenize = 'unicode_bigram'
         );",
    )?;
    Ok(connection)
}
```

Registration is connection-local. Register again for replacement connections,
connection pools, and in-memory test databases. Registration failure should be
treated as an initialization error; do not silently open the same index with a
different tokenizer.

After registration, use ordinary FTS5 SQL:

```sql
INSERT INTO passages_fts(text) VALUES ('全文検索を使う');

SELECT rowid, text
FROM passages_fts
WHERE passages_fts MATCH 'text : "全文検索"';
```

## C/C++: static registration

Build both library variants and the tests:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Link `fts5_bigram_static` (`libfts5_bigram.a` or `fts5_bigram.lib`) into the
application alongside the application's SQLite library. Include
`include/sqlite3_fts5_bigram.h`, then register after `sqlite3_open()` and before
using the FTS schema:

```c
#include "sqlite3_fts5_bigram.h"

char *message = NULL;
int result = sqlite3_fts5bigram_register(db, &message);
if (result != SQLITE_OK) {
    /* Log or copy message before releasing it. */
    sqlite3_free(message);
    return result;
}
```

The caller owns a non-NULL error message and releases it with `sqlite3_free()`.

## Loadable extension

The CMake build also creates one dynamic artifact:

- Linux: `build/fts5_bigram.so`
- macOS: `build/fts5_bigram.dylib`
- Windows multi-config generators: `build/Release/fts5_bigram.dll`

Load it before creating or opening the FTS table. For example, with the SQLite
CLI on Linux:

```sh
sqlite3 search.db
```

```text
.load ./build/fts5_bigram
CREATE VIRTUAL TABLE passages_fts USING fts5(
  text,
  tokenize = 'unicode_bigram'
);
```

SQLite derives and calls the exported entry point
`sqlite3_fts5bigram_init`. Applications using a language binding must enable
extension loading only for the load operation and disable it immediately
afterward. Static registration is preferred when the application controls its
SQLite build.

## Development

```sh
cargo fmt --check
cargo clippy --all-targets -- -D warnings
cargo test --all-targets
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

CI runs static and loadable integration tests on Linux, macOS, and Windows.
