package modernc

import (
	"database/sql"
	"testing"

	_ "modernc.org/sqlite"
)

func TestUnicodeBigramIsRegisteredOnEveryModerncConnection(t *testing.T) {
	for range 2 {
		db, err := sql.Open("sqlite", ":memory:")
		if err != nil {
			t.Fatal(err)
		}
		if _, err := db.Exec(`
			CREATE VIRTUAL TABLE passages USING fts5(text, tokenize='unicode_bigram');
			INSERT INTO passages(text) VALUES ('全文検索を設計する'), ('ÄPFEL');
		`); err != nil {
			_ = db.Close()
			t.Fatal(err)
		}

		for _, testCase := range []struct {
			query string
			want  int
		}{
			{query: "設計", want: 1},
			{query: "ÄPFEL", want: 1},
			{query: "äpfel", want: 0},
		} {
			var count int
			if err := db.QueryRow(
				"SELECT count(*) FROM passages WHERE passages MATCH ?",
				`"`+testCase.query+`"`,
			).Scan(&count); err != nil {
				_ = db.Close()
				t.Fatal(err)
			}
			if count != testCase.want {
				_ = db.Close()
				t.Fatalf("query %q matched %d rows, want %d", testCase.query, count, testCase.want)
			}
		}
		if err := db.Close(); err != nil {
			t.Fatal(err)
		}
	}
}

func TestTokenizerName(t *testing.T) {
	if TokenizerName != "unicode_bigram" {
		t.Fatalf("TokenizerName = %q", TokenizerName)
	}
}
