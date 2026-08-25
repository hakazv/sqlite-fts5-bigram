use rusqlite::{params, Connection};

fn connection() -> Connection {
    let connection = Connection::open_in_memory().unwrap();
    assert!(rusqlite::version_number() >= 3_047_000);
    unsafe { sqlite_fts5_bigram::register(connection.handle().cast()).unwrap() };
    connection
}

#[test]
fn registers_and_rejects_arguments() {
    let connection = connection();
    connection
        .execute_batch("CREATE VIRTUAL TABLE ok USING fts5(text, tokenize='unicode_bigram');")
        .unwrap();
    assert!(connection
        .execute_batch(
            "CREATE VIRTUAL TABLE invalid USING fts5(\
                 text, tokenize='unicode_bigram unexpected'\
             );"
        )
        .is_err());
}

#[test]
fn tokenizes_documents_and_queries_as_adjacent_bigrams() {
    let connection = connection();
    connection
        .execute_batch(
            "CREATE VIRTUAL TABLE passages USING fts5(text, tokenize='unicode_bigram');
             INSERT INTO passages(text) VALUES
                ('全文検索を設計する'),
                ('全文で別の検査をしてから検索する'),
                ('検索全文'),
                ('a\"b');",
        )
        .unwrap();

    for (query, expected) in [
        (
            "検索",
            vec![
                "全文検索を設計する",
                "全文で別の検査をしてから検索する",
                "検索全文",
            ],
        ),
        ("設計", vec!["全文検索を設計する"]),
        ("全文検索", vec!["全文検索を設計する"]),
        ("検索全文", vec!["検索全文"]),
        ("a\"b", vec!["a\"b"]),
    ] {
        let mut statement = connection
            .prepare("SELECT text FROM passages WHERE passages MATCH ?1 ORDER BY rowid")
            .unwrap();
        let actual = statement
            .query_map([sqlite_fts5_bigram::phrase_query(query).unwrap()], |row| {
                row.get::<_, String>(0)
            })
            .unwrap()
            .collect::<rusqlite::Result<Vec<_>>>()
            .unwrap();
        assert_eq!(actual, expected);
    }
}

#[test]
fn phrase_query_rejects_text_that_cannot_produce_a_bigram() {
    for text in ["", "検"] {
        assert_eq!(
            sqlite_fts5_bigram::phrase_query(text),
            Err(sqlite_fts5_bigram::PhraseQueryError::TooShort)
        );
    }
}

#[test]
fn external_content_triggers_and_rebuild_stay_in_sync() {
    let connection = connection();
    connection
        .execute_batch(
            "CREATE TABLE passages(id INTEGER PRIMARY KEY, text TEXT NOT NULL);
             CREATE VIRTUAL TABLE passages_fts USING fts5(
                text, content='passages', content_rowid='id', tokenize='unicode_bigram'
             );
             CREATE TRIGGER passages_ai AFTER INSERT ON passages BEGIN
                INSERT INTO passages_fts(rowid, text) VALUES (new.id, new.text);
             END;
             CREATE TRIGGER passages_ad AFTER DELETE ON passages BEGIN
                INSERT INTO passages_fts(passages_fts, rowid, text)
                VALUES ('delete', old.id, old.text);
             END;
             CREATE TRIGGER passages_au AFTER UPDATE ON passages BEGIN
                INSERT INTO passages_fts(passages_fts, rowid, text)
                VALUES ('delete', old.id, old.text);
                INSERT INTO passages_fts(rowid, text) VALUES (new.id, new.text);
             END;",
        )
        .unwrap();
    connection
        .execute("INSERT INTO passages(text) VALUES (?1)", ["全文検索"])
        .unwrap();
    assert_eq!(match_count(&connection, "検索"), 1);
    connection
        .execute("UPDATE passages SET text = ?1 WHERE id = 1", ["再設計"])
        .unwrap();
    assert_eq!(match_count(&connection, "検索"), 0);
    assert_eq!(match_count(&connection, "設計"), 1);
    connection
        .execute("DELETE FROM passages WHERE id = 1", [])
        .unwrap();
    assert_eq!(match_count(&connection, "設計"), 0);
    connection
        .execute("INSERT INTO passages(text) VALUES (?1)", ["再設計"])
        .unwrap();
    connection
        .execute(
            "INSERT INTO passages_fts(passages_fts) VALUES ('rebuild')",
            [],
        )
        .unwrap();
    assert_eq!(match_count(&connection, "設計"), 1);
}

#[test]
fn registration_is_per_connection() {
    let first = connection();
    let second = connection();
    for connection in [&first, &second] {
        connection
            .execute_batch(
                "CREATE VIRTUAL TABLE passages USING fts5(text, tokenize='unicode_bigram');",
            )
            .unwrap();
    }
}

fn match_count(connection: &Connection, query: &str) -> i64 {
    connection
        .query_row(
            "SELECT count(*) FROM passages_fts WHERE passages_fts MATCH ?1",
            params![sqlite_fts5_bigram::phrase_query(query).unwrap()],
            |row| row.get(0),
        )
        .unwrap()
}
