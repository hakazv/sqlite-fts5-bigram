cmake_minimum_required(VERSION 3.20)
project(sqlite_fts5_bigram VERSION 0.4.0 LANGUAGES C)

include(GNUInstallDirs)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# 自分の C。警告はここにだけ効かせる。
set(BIGRAM_SOURCES
    csrc/fts5_bigram.c
    csrc/unicode_bigram.c
    csrc/unicode_lower.c
    csrc/unicode_norm.c
    csrc/unicode_bigram_composed.c
)

# vendoring した utf8proc は object library に分け、こちらの警告設定から外す。
# 手を入れないコードに警告を出しても直せず、自分の警告が埋もれるだけになる。
# object を $<TARGET_OBJECTS:> で埋め込むだけなので link はしない。link すると
# install(EXPORT) がこの内部ターゲットまで export set に要求してくる。
add_library(bigram_utf8proc OBJECT third_party/utf8proc/utf8proc.c)
target_compile_definitions(bigram_utf8proc PRIVATE UTF8PROC_STATIC)
target_include_directories(bigram_utf8proc PRIVATE third_party/utf8proc)
set_target_properties(bigram_utf8proc PROPERTIES POSITION_INDEPENDENT_CODE ON)

# cc crate の warnings(true) と同じ水準を CMake 側でも掛ける。警告のまま止めないのは、
# 新しいコンパイラで利用者のビルドが落ちるのを避けるため (CI では出力に残る)。
if(MSVC)
    set(BIGRAM_WARNINGS /W4)
else()
    set(BIGRAM_WARNINGS -Wall -Wextra)
endif()

add_library(fts5_bigram_static STATIC ${BIGRAM_SOURCES} $<TARGET_OBJECTS:bigram_utf8proc>)
add_library(sqlite_fts5_bigram::static ALIAS fts5_bigram_static)
target_compile_options(fts5_bigram_static PRIVATE ${BIGRAM_WARNINGS})
target_compile_definitions(fts5_bigram_static PRIVATE UTF8PROC_STATIC)
target_compile_definitions(fts5_bigram_static PRIVATE SQLITE_CORE)
target_include_directories(
    fts5_bigram_static
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE csrc third_party/sqlite third_party/utf8proc
)
set_target_properties(
    fts5_bigram_static
    PROPERTIES EXPORT_NAME static OUTPUT_NAME fts5_bigram
)

add_library(fts5_bigram MODULE ${BIGRAM_SOURCES} $<TARGET_OBJECTS:bigram_utf8proc>)
target_compile_options(fts5_bigram PRIVATE ${BIGRAM_WARNINGS})
target_compile_definitions(fts5_bigram PRIVATE UTF8PROC_STATIC)
target_compile_definitions(fts5_bigram PRIVATE FTS5_BIGRAM_BUILD_EXTENSION)
target_include_directories(fts5_bigram PRIVATE csrc include third_party/sqlite third_party/utf8proc)
set_target_properties(fts5_bigram PROPERTIES PREFIX "")
if(APPLE)
    set_target_properties(fts5_bigram PROPERTIES SUFFIX ".dylib")
    target_link_options(fts5_bigram PRIVATE "-undefined" "dynamic_lookup")
endif()

include(CMakePackageConfigHelpers)
set(BIGRAM_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/sqlite_fts5_bigram")
configure_package_config_file(
    cmake/sqlite_fts5_bigramConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/sqlite_fts5_bigramConfig.cmake"
    INSTALL_DESTINATION "${BIGRAM_CMAKE_INSTALL_DIR}"
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/sqlite_fts5_bigramConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMinorVersion
)

install(
    TARGETS fts5_bigram_static
    EXPORT sqlite_fts5_bigramTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
install(
    TARGETS fts5_bigram
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
install(
    FILES include/sqlite3_fts5_bigram.h
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
install(
    EXPORT sqlite_fts5_bigramTargets
    NAMESPACE sqlite_fts5_bigram::
    DESTINATION "${BIGRAM_CMAKE_INSTALL_DIR}"
)
install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/sqlite_fts5_bigramConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/sqlite_fts5_bigramConfigVersion.cmake"
    DESTINATION "${BIGRAM_CMAKE_INSTALL_DIR}"
)
install(FILES LICENSE DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/sqlite_fts5_bigram")

include(CTest)
if(BUILD_TESTING)
    add_executable(
        tokenizer_test
        tests/tokenizer.c
        csrc/unicode_bigram.c
        csrc/unicode_lower.c
        csrc/unicode_norm.c
        csrc/unicode_bigram_composed.c
        $<TARGET_OBJECTS:bigram_utf8proc>
    )
    target_include_directories(tokenizer_test PRIVATE csrc third_party/utf8proc)
    target_compile_definitions(tokenizer_test PRIVATE UTF8PROC_STATIC)
    target_compile_options(tokenizer_test PRIVATE ${BIGRAM_WARNINGS})
    if(MSVC)
        target_compile_options(tokenizer_test PRIVATE /UNDEBUG)
    else()
        target_compile_options(tokenizer_test PRIVATE -UNDEBUG)
    endif()
    add_test(
        NAME tokenizer_core
        COMMAND tokenizer_test
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/tokenizer-corpus.tsv
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/casefold-corpus.tsv
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/unicode/NormalizationTest.txt
    )
endif()
