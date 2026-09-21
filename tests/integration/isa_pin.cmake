if(NOT DEFINED ABCD_SOURCE_DIR OR NOT DEFINED ABCD_TEST_BINARY_DIR)
  message(FATAL_ERROR "ABCD_SOURCE_DIR and ABCD_TEST_BINARY_DIR are required")
endif()

set(root "${ABCD_TEST_BINARY_DIR}/isa-pin")
set(source "${root}/source")
set(build "${root}/build")
set(isa "${root}/isa")
file(REMOVE_RECURSE "${root}")
file(MAKE_DIRECTORY "${source}/tools/templates" "${isa}")

set(pinned "${ABCD_SOURCE_DIR}/../arkruntime/arkcompiler/runtime_core/isa")
foreach(name IN ITEMS isa.yaml isapi.rb gen.rb)
  file(COPY "${pinned}/${name}" DESTINATION "${isa}")
endforeach()
foreach(name IN ITEMS isa_opcode.h.erb isa_table.inc.erb isa_cases.inc.erb
                      isa_flow.inc.erb)
  file(COPY "${ABCD_SOURCE_DIR}/tools/templates/${name}"
       DESTINATION "${source}/tools/templates")
endforeach()
file(WRITE "${source}/CMakeLists.txt"
     "cmake_minimum_required(VERSION 3.20)\n"
     "project(isa_pin LANGUAGES CXX)\n"
     "include(\"${ABCD_SOURCE_DIR}/cmake/IsaDependency.cmake\")\n")

execute_process(COMMAND "${CMAKE_COMMAND}" -S "${source}" -B "${build}"
                        "-DABCD_ISA_ROOT=${isa}"
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
  file(APPEND "${isa}/${name}" "\n# pin-change regression fixture\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" --build "${build}"
                          --target abcd_isa_generated
                  RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(status EQUAL 0 OR NOT "${output}${error}" MATCHES "Pinned ISA input changed")
    message(FATAL_ERROR "changed ${name} was not rejected:\n${output}\n${error}")
  endif()
  file(COPY_FILE "${pinned}/${name}" "${isa}/${name}")
  execute_process(COMMAND "${CMAKE_COMMAND}" -S "${source}" -B "${build}"
                          "-DABCD_ISA_ROOT=${isa}"
                  RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(NOT status EQUAL 0)
    message(FATAL_ERROR "restored ${name} configure failed:\n${output}\n${error}")
  endif()
endforeach()
