cmake_minimum_required(VERSION 3.20)
project(sqlite_fts5_bigram VERSION 0.2.0 LANGUAGES C)

include(GNUInstallDirs)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(BIGRAM_SOURCES
    csrc/fts5_bigram.c
    csrc/unicode_bigram.c
    csrc/unicode_lower.c
)

add_library(fts5_bigram_static STATIC ${BIGRAM_SOURCES})
add_library(sqlite_fts5_bigram::static ALIAS fts5_bigram_static)
target_compile_definitions(fts5_bigram_static PRIVATE SQLITE_CORE)
target_include_directories(
    fts5_bigram_static
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE csrc third_party/sqlite
)
set_target_properties(
    fts5_bigram_static
    PROPERTIES EXPORT_NAME static OUTPUT_NAME fts5_bigram
)

add_library(fts5_bigram MODULE ${BIGRAM_SOURCES})
target_compile_definitions(fts5_bigram PRIVATE FTS5_BIGRAM_BUILD_EXTENSION)
target_include_directories(fts5_bigram PRIVATE csrc include third_party/sqlite)
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
    )
    target_include_directories(tokenizer_test PRIVATE csrc)
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
    )
endif()
