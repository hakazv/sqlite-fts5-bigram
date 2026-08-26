package modernc

import (
	"bufio"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"testing"
)

// 実装が 2 つある以上、両者が同じ規則に従うことは実装同士の突き合わせではなく、
// 仕様への適合として担保する。C 側 (csrc/unicode_norm.c) も同じファイルを読む。
//
// この検査が捕まえるのは正規化そのものの誤りで、トークンの切り方は
// tests/tokenizer-corpus.tsv が両実装に対して見ている。
func TestNormalizationMatchesUnicodeConformanceData(t *testing.T) {
	path := filepath.Join("..", "..", "third_party", "unicode", "NormalizationTest.txt")
	file, err := os.Open(path)
	if err != nil {
		t.Fatal(err)
	}
	defer file.Close()

	listed := map[rune]bool{}
	checked := 0
	scanner := bufio.NewScanner(file)
	scanner.Buffer(make([]byte, 1<<20), 1<<20)
	for scanner.Scan() {
		line := scanner.Text()
		if index := strings.IndexByte(line, '#'); index >= 0 {
			line = line[:index]
		}
		line = strings.TrimSpace(line)
		if line == "" || strings.HasPrefix(line, "@") {
			continue
		}
		fields := strings.Split(line, ";")
		if len(fields) < 5 {
			t.Fatalf("invalid conformance line %q", line)
		}
		sources := make([]string, 5)
		for index := range sources {
			sources[index] = codePoints(t, fields[index])
		}
		for _, r := range sources[0] {
			listed[r] = true
		}
		// NFC の適合条件: c2 == toNFC(c1) == toNFC(c2) == toNFC(c3)
		//                 c4 == toNFC(c4) == toNFC(c5)
		for _, pair := range [][2]string{
			{sources[0], sources[1]}, {sources[1], sources[1]}, {sources[2], sources[1]},
			{sources[3], sources[3]}, {sources[4], sources[3]},
		} {
			checked++
			got := composeForTokenizing([]byte(pair[0])).text
			if string(got) != pair[1] {
				t.Fatalf("NFC(%x) = %x, want %x", pair[0], got, pair[1])
			}
		}
	}
	if err := scanner.Err(); err != nil {
		t.Fatal(err)
	}
	if checked == 0 {
		t.Fatal("no conformance assertions were read")
	}

	// 一覧に無いコードポイントは、正規化で自分自身へ写らなければならない。
	// 表の作り漏れや余計な項目を全域で捕まえる。
	for code := rune(0); code <= 0x10ffff; code++ {
		if code >= 0xd800 && code <= 0xdfff || listed[code] {
			continue
		}
		source := string(code)
		if got := composeForTokenizing([]byte(source)).text; string(got) != source {
			t.Fatalf("NFC(U+%04X) = %x, want unchanged", code, got)
		}
	}
}

func codePoints(t *testing.T, field string) string {
	t.Helper()
	var builder strings.Builder
	for _, value := range strings.Fields(field) {
		code, err := strconv.ParseUint(value, 16, 32)
		if err != nil {
			t.Fatalf("invalid code point %q: %v", value, err)
		}
		builder.WriteRune(rune(code))
	}
	return builder.String()
}
