use std::path::Path;

#[test]
fn sqlite_loads_the_cmake_extension_by_derived_entry_point() {
    let Ok(extension) = std::env::var("FTS5_BIGRAM_EXTENSION") else {
        return;
    };
    assert!(Path::new(&extension).is_file(), "missing {extension}");
    let connection = rusqlite::Connection::open_in_memory().unwrap();
    unsafe {
        connection.load_extension_enable().unwrap();
        connection.load_extension(&extension, None::<&str>).unwrap();
        connection.load_extension_disable().unwrap();
    }
    connection
        .execute_batch(
            "CREATE VIRTUAL TABLE passages USING fts5(text, tokenize='unicode_bigram');
             INSERT INTO passages(text) VALUES ('全文検索');",
        )
        .unwrap();
    let count: i64 = connection
        .query_row(
            "SELECT count(*) FROM passages WHERE passages MATCH '\"検索\"'",
            [],
            |row| row.get(0),
        )
        .unwrap();
    assert_eq!(count, 1);
}
