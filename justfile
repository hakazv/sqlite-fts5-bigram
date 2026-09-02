# 3 つのツールチェイン (cargo / CMake / Go) を持つので、片方の変更が別方を静かに壊せる。
# 触ったら check を通す。CI もこの recipe を呼ぶので、手元と CI で手順がずれない。
#
# just を必須にはしない。cargo / cmake / go を直接叩く経路はそのまま使える。
# ここにあるのは「正しい呼び方」の控えで、置き換えではない。

# 3 つのツールチェインすべてを通す
check: test-rust test-loadable test-c test-go package

# CMake: static / loadable のビルド
build-c:
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release

# cargo: 整形・lint・静的登録
test-rust:
    cargo fmt --check
    cargo clippy --all-targets -- -D warnings
    cargo test --test sqlite

# loadable extension を読み込ませて試す (先に build-c が要る)
test-loadable: build-c
    #!/usr/bin/env bash
    set -euo pipefail
    extension="$(find build -type f \( -name 'fts5_bigram.so' -o -name 'fts5_bigram.dylib' -o -name 'fts5_bigram.dll' \) | head -n 1)"
    test -n "$extension"
    FTS5_BIGRAM_EXTENSION="$extension" cargo test --test loadable

# C の中核: corpus・大文字小文字・Unicode 適合性テスト
test-c: build-c
    ctest --test-dir build -C Release --output-on-failure

# install(EXPORT) の壊れはここでしか出ないので、独立した recipe にしてある
# (内部ターゲットを export set に要求する等)。
#
# 配布物として install し、利用者側の CMake から使えることを確かめる
package: build-c
    cmake --install build --config Release --prefix install
    cmake -S tests/cmake-package -B build-package-test -DCMAKE_PREFIX_PATH={{ justfile_directory() }}/install
    cmake --build build-package-test --config Release

# -race: checkptr が付く。tokenizer は翻訳 C との境界でポインタを渡すので、
# 「C から見て正当な確保でない領域を SQLite に読ませる」類の誤りはここでしか出ない。
#
# Go: 整形・vet・テスト (corpus / 適合性 / 生成表の追随)
test-go:
    #!/usr/bin/env bash
    set -euo pipefail
    unformatted="$(gofmt -l .)"
    if [ -n "$unformatted" ]; then
        echo "gofmt needed for:"
        echo "$unformatted"
        exit 1
    fi
    go vet ./...
    go test -race ./...

# Go が Unicode の版を上げたときに要る (internal/generateunicode の test が教える)。
#
# C が使う Unicode の表を作り直す
generate:
    go generate ./...
