fn main() {
    for path in [
        "csrc/fts5_bigram.c",
        "csrc/unicode_bigram.c",
        "csrc/unicode_bigram.h",
        "csrc/unicode_lower.c",
        "csrc/unicode_lower.h",
        "csrc/unicode_lower_table.inc",
        "csrc/unicode_norm.c",
        "csrc/unicode_norm.h",
        "csrc/unicode_bigram_composed.c",
        "csrc/unicode_bigram_composed.h",
        "third_party/utf8proc/utf8proc.c",
        "include/sqlite3_fts5_bigram.h",
        "third_party/sqlite/sqlite3.h",
    ] {
        println!("cargo:rerun-if-changed={path}");
    }

    // vendoring した utf8proc はこちらの警告設定で通さない。手を入れないコードに対して
    // 警告を出しても直せず、自分の警告が埋もれるだけになる。
    cc::Build::new()
        .file("third_party/utf8proc/utf8proc.c")
        .include("third_party/utf8proc")
        .define("UTF8PROC_STATIC", None)
        .warnings(false)
        .compile("utf8proc");

    cc::Build::new()
        .file("csrc/fts5_bigram.c")
        .file("csrc/unicode_bigram.c")
        .file("csrc/unicode_lower.c")
        .file("csrc/unicode_norm.c")
        .file("csrc/unicode_bigram_composed.c")
        .include("csrc")
        .include("third_party/utf8proc")
        .include("include")
        .include("third_party/sqlite")
        .define("SQLITE_CORE", None)
        .define("UTF8PROC_STATIC", None)
        .warnings(true)
        .compile("fts5_bigram");
}
