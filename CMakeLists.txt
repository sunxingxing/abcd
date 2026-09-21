cmake_minimum_required(VERSION 3.20)

project(abcd LANGUAGES CXX)

add_library(abcd_abc
  src/abc/byte_reader.cc
  src/abc/header.cc
)
target_compile_features(abcd_abc PUBLIC cxx_std_20)
target_include_directories(abcd_abc PUBLIC include)

find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto)
find_package(ZLIB REQUIRED)
add_library(abcd_corpus src/corpus/abc_file.cc)
target_link_libraries(abcd_corpus PUBLIC abcd_abc
  PRIVATE OpenSSL::Crypto ZLIB::ZLIB)

include(CTest)
if(BUILD_TESTING)
  add_executable(byte_reader_test tests/unit/abc/byte_reader_test.cc)
  target_link_libraries(byte_reader_test PRIVATE abcd_abc)
  add_test(NAME byte_reader_test COMMAND byte_reader_test)

  add_executable(header_test tests/unit/abc/header_test.cc)
  target_link_libraries(header_test PRIVATE abcd_abc)
  add_test(NAME header_test COMMAND header_test)

  add_executable(abc_file_test tests/unit/corpus/abc_file_test.cc)
  target_link_libraries(abc_file_test PRIVATE abcd_corpus)
  add_test(NAME abc_file_test COMMAND abc_file_test)
endif()
