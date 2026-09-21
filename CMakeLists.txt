cmake_minimum_required(VERSION 3.20)

project(abcd LANGUAGES CXX)

add_library(abcd_abc
  src/abc/byte_reader.cc
  src/abc/header.cc
)
target_compile_features(abcd_abc PUBLIC cxx_std_20)
target_include_directories(abcd_abc PUBLIC include)

include(CTest)
if(BUILD_TESTING)
  add_executable(byte_reader_test tests/unit/abc/byte_reader_test.cc)
  target_link_libraries(byte_reader_test PRIVATE abcd_abc)
  add_test(NAME byte_reader_test COMMAND byte_reader_test)

  add_executable(header_test tests/unit/abc/header_test.cc)
  target_link_libraries(header_test PRIVATE abcd_abc)
  add_test(NAME header_test COMMAND header_test)
endif()
