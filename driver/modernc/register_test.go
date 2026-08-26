package modernc

import (
	"bufio"
	"bytes"
	"database/sql"
	"encoding/hex"
	"os"
	"path/filepath"
	"strings"
	"testing"

	fts5bigram "github.com/hakazv/sqlite-fts5-bigram"
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
			INSERT INTO passages(text) VALUES ('全文検索を設計する'), ('ÄPFEL'), ('a"b');
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
			{query: "äpfel", want: 1},
			{query: `a"b`, want: 1},
		} {
			query, err := fts5bigram.PhraseQuery(testCase.query)
			if err != nil {
				_ = db.Close()
				t.Fatal(err)
			}
			var count int
			if err := db.QueryRow(
				"SELECT count(*) FROM passages WHERE passages MATCH ?",
				query,
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

func TestTokenizerOptions(t *testing.T) {
	db, err := sql.Open("sqlite", ":memory:")
	if err != nil {
		t.Fatal(err)
	}
	defer db.Close()

	if _, err := db.Exec(`
		CREATE VIRTUAL TABLE sensitive USING fts5(
			text, tokenize='unicode_bigram case_sensitive 1'
		);
		INSERT INTO sensitive(text) VALUES ('ÄPFEL');
	`); err != nil {
		t.Fatal(err)
	}
	query, err := fts5bigram.PhraseQuery("äpfel")
	if err != nil {
		t.Fatal(err)
	}
	var count int
	if err := db.QueryRow(
		"SELECT count(*) FROM sensitive WHERE sensitive MATCH ?",
		query,
	).Scan(&count); err != nil {
		t.Fatal(err)
	}
	if count != 0 {
		t.Fatalf("case-sensitive query matched %d rows, want 0", count)
	}

	for _, specification := range []string{
		"unicode_bigram case_sensitive",
		"unicode_bigram case_sensitive 2",
		"unicode_bigram unexpected 1",
	} {
		if _, err := db.Exec(
			"CREATE VIRTUAL TABLE invalid USING fts5(text, tokenize = '" + specification + "')",
		); err == nil {
			t.Fatalf("tokenize=%q succeeded, want error", specification)
		}
	}
}

func TestTokenizerName(t *testing.T) {
	if TokenizerName != "unicode_bigram" {
		t.Fatalf("TokenizerName = %q", TokenizerName)
	}
}

func TestTokenizerMatchesSharedCorpus(t *testing.T) {
	corpusPath := filepath.Join("..", "..", "tests", "tokenizer-corpus.tsv")
	file, err := os.Open(corpusPath)
	if err != nil {
		t.Fatal(err)
	}
	defer file.Close()

	scanner := bufio.NewScanner(file)
	for scanner.Scan() {
		line := scanner.Text()
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		fields := strings.Split(line, "\t")
		if len(fields) < 3 || len(fields) > 4 {
			t.Fatalf("invalid corpus line %q", line)
		}
		name, inputHex, expectedResult := fields[0], fields[1], fields[2]
		var tokenHexes string
		if len(fields) == 4 {
			tokenHexes = fields[3]
		}
		t.Run(name, func(t *testing.T) {
			input, err := hex.DecodeString(inputHex)
			if err != nil {
				t.Fatal(err)
			}
			var actual [][]byte
			// token は正規化後の文字列で、入力の部分列とは限らない。
			result := walkBigrams(input, func(token []byte, _, _ int) int32 {
				actual = append(actual, bytes.Clone(token))
				return 0
			})
			if expectedResult == "invalid_utf8" {
				if result != sqliteError || len(actual) != 0 {
					t.Fatalf("result = %d, tokens = %x; want invalid UTF-8", result, actual)
				}
				return
			}
			if expectedResult != "ok" {
				t.Fatalf("unknown expected result %q", expectedResult)
			}
			if result != 0 {
				t.Fatalf("result = %d, want 0", result)
			}

			var expected [][]byte
			if tokenHexes != "" {
				for _, tokenHex := range strings.Split(tokenHexes, ",") {
					token, err := hex.DecodeString(tokenHex)
					if err != nil {
						t.Fatal(err)
					}
					expected = append(expected, token)
				}
			}
			if len(actual) != len(expected) {
				t.Fatalf("tokens = %x, want %x", actual, expected)
			}
			for index := range expected {
				if !bytes.Equal(actual[index], expected[index]) {
					t.Fatalf("token %d = %x, want %x", index, actual[index], expected[index])
				}
			}
		})
	}
	if err := scanner.Err(); err != nil {
		t.Fatal(err)
	}
}

func TestCaseFoldingMatchesSharedCorpus(t *testing.T) {
	corpusPath := filepath.Join("..", "..", "tests", "casefold-corpus.tsv")
	file, err := os.Open(corpusPath)
	if err != nil {
		t.Fatal(err)
	}
	defer file.Close()

	scanner := bufio.NewScanner(file)
	for scanner.Scan() {
		line := scanner.Text()
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		fields := strings.Split(line, "\t")
		if len(fields) != 3 {
			t.Fatalf("invalid corpus line %q", line)
		}
		name, inputHex, expectedHex := fields[0], fields[1], fields[2]
		t.Run(name, func(t *testing.T) {
			input, err := hex.DecodeString(inputHex)
			if err != nil {
				t.Fatal(err)
			}
			expected, err := hex.DecodeString(expectedHex)
			if err != nil {
				t.Fatal(err)
			}
			if actual := lowercaseBigram(input); !bytes.Equal(actual, expected) {
				t.Fatalf("lowercase = %x, want %x", actual, expected)
			}
		})
	}
	if err := scanner.Err(); err != nil {
		t.Fatal(err)
	}
}
