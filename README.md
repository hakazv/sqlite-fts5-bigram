# sqlite-fts5-bigram

`sqlite-fts5-bigram` provides the SQLite FTS5 tokenizer
`unicode_bigram`. It emits every pair of adjacent UTF-8 code points without
normalization, case folding, or separator handling.

The tokenizer requires SQLite 3.47.0 or newer and the FTS5 tokenizer v2 API.
Invalid UTF-8 is rejected with `SQLITE_ERROR`. Tokenizer arguments are not
supported.

## Rust static registration

The Cargo crate compiles the C sources with `SQLITE_CORE` and does not link a
second SQLite library. Register it on each connection before creating or
opening an FTS5 table that uses the tokenizer:

```rust
let connection = rusqlite::Connection::open_in_memory()?;
unsafe { sqlite_fts5_bigram::register(connection.handle().cast())? };
```

Pin Git dependencies to a verified commit.

## CMake

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

This creates a static library and a loadable `fts5_bigram` extension. SQLite's
derived entry-point lookup resolves `sqlite3_fts5bigram_init`. Static consumers
call `sqlite3_fts5bigram_register`.

