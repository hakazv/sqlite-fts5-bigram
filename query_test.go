package fts5bigram

import (
	"errors"
	"testing"
)

func TestPhraseQuery(t *testing.T) {
	for _, testCase := range []struct {
		name string
		text string
		want string
	}{
		{name: "Japanese", text: "全文検索", want: `"全文検索"`},
		{name: "quotes", text: `a"b`, want: `"a""b"`},
		{name: "non-BMP", text: "😀A", want: `"😀A"`},
		{name: "combining mark", text: "e\u0301", want: `"é"`},
	} {
		t.Run(testCase.name, func(t *testing.T) {
			got, err := PhraseQuery(testCase.text)
			if err != nil {
				t.Fatal(err)
			}
			if got != testCase.want {
				t.Fatalf("PhraseQuery(%q) = %q, want %q", testCase.text, got, testCase.want)
			}
		})
	}
}

func TestPhraseQueryRejectsUnsearchableInput(t *testing.T) {
	for _, testCase := range []struct {
		name string
		text string
		want error
	}{
		{name: "empty", text: "", want: ErrPhraseTooShort},
		{name: "one code point", text: "検", want: ErrPhraseTooShort},
		{name: "invalid UTF-8", text: string([]byte{0xff}), want: ErrInvalidUTF8},
	} {
		t.Run(testCase.name, func(t *testing.T) {
			_, err := PhraseQuery(testCase.text)
			if !errors.Is(err, testCase.want) {
				t.Fatalf("PhraseQuery returned %v, want %v", err, testCase.want)
			}
		})
	}
}
