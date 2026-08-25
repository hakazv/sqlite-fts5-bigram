cmake_minimum_required(VERSION 3.20)
project(sqlite_fts5_bigram_package_test LANGUAGES C CXX)

find_package(sqlite_fts5_bigram CONFIG REQUIRED)

add_library(package_consumer OBJECT consumer.c)
target_link_libraries(package_consumer PRIVATE sqlite_fts5_bigram::static)

add_library(package_consumer_cpp OBJECT consumer.cpp)
target_link_libraries(package_consumer_cpp PRIVATE sqlite_fts5_bigram::static)
