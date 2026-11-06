package modernc

import (
	"testing"
	"unicode/utf8"

	"golang.org/x/text/unicode/norm"
)

func FuzzComposeForTokenizing(f *testing.F) {
	for _, seed := range [][]byte{
		[]byte("全文検索"),
		[]byte("e\u0301"),
		[]byte("ガ"),
		[]byte("カ\u3099"),
		[]byte(""),
		[]byte{0xff, 0xfe},
		[]byte("a\x00b"),
	} {
		f.Add(seed)
	}
	f.Fuzz(func(t *testing.T, data []byte) {
		result := composeForTokenizing(data)
		if !norm.NFC.IsNormal(result.text) {
			t.Fatalf("output not normalized to NFC for input %q", data)
		}
		if len(result.starts) != len(result.ends) {
			t.Fatalf("mismatched starts (%d) and ends (%d)", len(result.starts), len(result.ends))
		}
		for i := range result.starts {
			s, e := result.starts[i], result.ends[i]
			if s < 0 || s > len(data) || e < 0 || e > len(data) || s > e {
				t.Fatalf("invalid boundary [%d, %d] for len %d", s, e, len(data))
			}
		}
		if len(result.starts) > 0 {
			count := utf8.RuneCount(result.text)
			if count != len(result.starts) {
				t.Fatalf("rune count %d != offsets count %d", count, len(result.starts))
			}
		}
	})
}
