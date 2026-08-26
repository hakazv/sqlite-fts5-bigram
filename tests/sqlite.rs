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
    for specification in [
        "unicode_bigram case_sensitive",
        "unicode_bigram case_sensitive 2",
        "unicode_bigram unexpected 1",
    ] {
        assert!(connection
            .execute_batch(&format!(
                "CREATE VIRTUAL TABLE invalid_options USING fts5(\
                     text, tokenize='{specification}'\
                 );"
            ))
            .is_err());
    }
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
fn folds_case_by_default_and_can_be_case_sensitive() {
    let connection = connection();
    connection
        .execute_batch(
            "CREATE VIRTUAL TABLE folded USING fts5(text, tokenize='unicode_bigram');
             CREATE VIRTUAL TABLE sensitive USING fts5(
                 text, tokenize='unicode_bigram case_sensitive 1'
             );
             INSERT INTO folded(text) VALUES ('Alpha ÄPFEL');
             INSERT INTO sensitive(text) VALUES ('Alpha ÄPFEL');",
        )
        .unwrap();

    let query = sqlite_fts5_bigram::phrase_query("äpfel").unwrap();
    let count = |table: &str| {
        connection
            .query_row(
                &format!("SELECT count(*) FROM {table} WHERE {table} MATCH ?1"),
                [&query],
                |row| row.get::<_, i64>(0),
            )
            .unwrap()
    };
    assert_eq!(count("folded"), 1);
    assert_eq!(count("sensitive"), 0);
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

/// 分解済みの本文と合成済みのクエリが、どちらの向きでも噛み合うこと。
///
/// 正規化を窓の前に置かないと、結合文字が独立したトークン単位になり、取りこぼし
/// (合成済みのクエリが当たらない) と誤ヒット (濁点を落とした語で当たる) が同時に出る。
#[test]
fn matches_across_unicode_normalization_forms() {
    let connection = connection();
    // 「ガイドライン」を分解済みで格納する。
    let decomposed = "\u{30ab}\u{3099}\u{30a4}\u{30c8}\u{3099}\u{30e9}\u{30a4}\u{30f3}";
    connection
        .execute_batch("CREATE VIRTUAL TABLE notes USING fts5(text, tokenize='unicode_bigram');")
        .unwrap();
    connection
        .execute("INSERT INTO notes(text) VALUES (?1)", [decomposed])
        .unwrap();

    let count = |needle: &str| {
        let query = sqlite_fts5_bigram::phrase_query(needle).unwrap();
        connection
            .query_row(
                "SELECT count(*) FROM notes WHERE notes MATCH ?1",
                [&query],
                |row| row.get::<_, i64>(0),
            )
            .unwrap()
    };

    assert_eq!(count("\u{30ac}\u{30a4}"), 1, "composed query should match");
    assert_eq!(
        count("\u{30ab}\u{3099}\u{30a4}"),
        1,
        "decomposed query should match"
    );
    // 濁点を落とした「カイ」は別語なので当たってはいけない。
    assert_eq!(count("\u{30ab}\u{30a4}"), 0, "dakuten must not be ignored");
}
