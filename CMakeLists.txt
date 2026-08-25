cmake_minimum_required(VERSION 3.20)
project(sqlite_fts5_bigram VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(BIGRAM_SOURCES
    csrc/fts5_bigram.c
    csrc/unicode_bigram.c
)

add_library(fts5_bigram_static STATIC ${BIGRAM_SOURCES})
target_compile_definitions(fts5_bigram_static PRIVATE SQLITE_CORE)
target_include_directories(fts5_bigram_static PRIVATE csrc include third_party/sqlite)
set_target_properties(fts5_bigram_static PROPERTIES OUTPUT_NAME fts5_bigram)

add_library(fts5_bigram MODULE ${BIGRAM_SOURCES})
target_include_directories(fts5_bigram PRIVATE csrc include third_party/sqlite)
set_target_properties(fts5_bigram PROPERTIES PREFIX "")
if(APPLE)
    set_target_properties(fts5_bigram PROPERTIES SUFFIX ".dylib")
    target_link_options(fts5_bigram PRIVATE "-undefined" "dynamic_lookup")
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(tokenizer_test tests/tokenizer.c csrc/unicode_bigram.c)
    target_include_directories(tokenizer_test PRIVATE csrc)
    add_test(NAME tokenizer_core COMMAND tokenizer_test)
endif()
