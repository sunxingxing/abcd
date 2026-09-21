cmake_minimum_required(VERSION 3.20)

project(abcd LANGUAGES CXX)

add_library(abcd_abc src/abc/byte_reader.cc)
target_compile_features(abcd_abc PUBLIC cxx_std_20)
target_include_directories(abcd_abc PUBLIC include)

include(CTest)
if(BUILD_TESTING)
  add_executable(byte_reader_test tests/unit/abc/byte_reader_test.cc)
  target_link_libraries(byte_reader_test PRIVATE abcd_abc)
  target_compile_options(byte_reader_test PRIVATE
    $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU,Clang,AppleClang>>:-UNDEBUG>)
  add_test(NAME byte_reader_test COMMAND byte_reader_test)
endif()
