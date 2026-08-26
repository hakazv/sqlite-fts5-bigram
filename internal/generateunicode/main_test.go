package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
	"unicode"
)

// 大文字小文字は 2 実装が別々の出所を持つ。C 側はこの表、Go 側は strings.ToLower で、
// 後者はビルドに使う Go に追随する。Go が Unicode の版を上げると、Go 側だけが変わって
// 表は据え置かれ、同じ入力から違うトークンが出る。
//
// 共有 corpus は決まった事例しか見ないので、該当文字が載っていなければ気づけない。
// 表そのものを突き合わせて、ずれた時点で落とす。
func TestLowercaseTableIsUpToDateWithTheRunningGo(t *testing.T) {
	path := filepath.Join("..", "..", lowercaseTablePath)
	committed, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	generated := lowercaseTable()
	if string(committed) == generated {
		return
	}

	committedHeader := strings.SplitN(string(committed), "\n", 2)[0]
	t.Fatalf(
		"%s is stale: it was generated for a different Unicode version than the Go building "+
			"the modernc driver (%s says %q, this Go has %s). The C and Go tokenizers would "+
			"fold case differently. Run `go generate ./...` and commit the result.",
		lowercaseTablePath, lowercaseTablePath, committedHeader, unicode.Version,
	)
}
