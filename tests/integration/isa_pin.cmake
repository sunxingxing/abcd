if(NOT DEFINED ABCD_SOURCE_DIR OR NOT DEFINED ABCD_TEST_BINARY_DIR
    OR NOT DEFINED ABCD_ISA_ROOT)
  message(FATAL_ERROR
          "ABCD_SOURCE_DIR, ABCD_TEST_BINARY_DIR, and ABCD_ISA_ROOT are required")
endif()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef nonce)
set(root "${ABCD_TEST_BINARY_DIR}/isa-pin-${nonce}")
set(source "${root}/source")
set(build "${root}/build")
set(isa "${root}/isa")
file(REMOVE_RECURSE "${root}")
file(MAKE_DIRECTORY "${source}/tools/templates" "${isa}")

set(pinned "${ABCD_ISA_ROOT}")
# The nested configure must use the same pinned Ruby as the parent build.
set(ruby_argument)
if(DEFINED ABCD_TEST_RUBY)
  list(APPEND ruby_argument "-DRuby_EXECUTABLE=${ABCD_TEST_RUBY}")
endif()
foreach(name IN ITEMS isa.yaml isapi.rb gen.rb)
  file(COPY "${pinned}/${name}" DESTINATION "${isa}")
endforeach()
foreach(name IN ITEMS isa_opcode.h.erb isa_table.inc.erb isa_cases.inc.erb
                      isa_flow.inc.erb access_catalog.inc.erb isa_catalog.inc.erb)
  file(COPY "${ABCD_SOURCE_DIR}/tools/templates/${name}"
       DESTINATION "${source}/tools/templates")
endforeach()
file(WRITE "${source}/CMakeLists.txt"
     "cmake_minimum_required(VERSION 3.20)\n"
     "project(isa_pin LANGUAGES CXX)\n"
     "include(\"${ABCD_SOURCE_DIR}/cmake/IsaDependency.cmake\")\n")

execute_process(COMMAND "${CMAKE_COMMAND}" -S "${source}" -B "${build}"
                        "-DABCD_ISA_ROOT=${isa}" ${ruby_argument}
                RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "valid pinned ISA configure failed:\n${output}\n${error}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${build}"
                        --target abcd_isa_generated
                RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "valid pinned ISA build failed:\n${output}\n${error}")
endif()

foreach(name IN ITEMS isa.yaml isapi.rb gen.rb)
  file(TIMESTAMP "${build}/generated/abcd/abc/generated_isa.h" before "%s")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1)
  file(APPEND "${isa}/${name}" "\n# pin-change regression fixture\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" --build "${build}" --parallel 2
                          --target abcd_isa_generated
                  RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(status EQUAL 0 OR NOT "${output}${error}" MATCHES "Pinned ISA input changed")
    message(FATAL_ERROR "changed ${name} was not rejected:\n${output}\n${error}")
  endif()
  file(TIMESTAMP "${build}/generated/abcd/abc/generated_isa.h" after "%s")
  if(NOT before STREQUAL after)
    message(FATAL_ERROR "generator ran before ${name} pin verification")
  endif()
  file(COPY "${pinned}/${name}" DESTINATION "${isa}")
  execute_process(COMMAND "${CMAKE_COMMAND}" -S "${source}" -B "${build}"
                          "-DABCD_ISA_ROOT=${isa}" ${ruby_argument}
                  RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(NOT status EQUAL 0)
    message(FATAL_ERROR "restored ${name} configure failed:\n${output}\n${error}")
  endif()
endforeach()
file(REMOVE_RECURSE "${root}")
