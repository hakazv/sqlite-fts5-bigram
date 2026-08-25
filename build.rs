fn main() {
    for path in [
        "csrc/fts5_bigram.c",
        "csrc/unicode_bigram.c",
        "csrc/unicode_bigram.h",
        "csrc/unicode_lower.c",
        "csrc/unicode_lower.h",
        "csrc/unicode_lower_table.inc",
        "include/sqlite3_fts5_bigram.h",
        "third_party/sqlite/sqlite3.h",
    ] {
        println!("cargo:rerun-if-changed={path}");
    }

    cc::Build::new()
        .file("csrc/fts5_bigram.c")
        .file("csrc/unicode_bigram.c")
        .file("csrc/unicode_lower.c")
        .include("csrc")
        .include("include")
        .include("third_party/sqlite")
        .define("SQLITE_CORE", None)
        .warnings(true)
        .compile("fts5_bigram");
}
