# Value-tree route CLI regression specification (corrections R1/R5/R6).
# It requires:
#   - prohibited, removed (legacy) and duplicate/conflicting option
#     combinations are rejected in both orders before any file access,
#     without receipts;
#   - --value-source yields a printed class-member slice bound to the frozen
#     pasteboard identity, with incomplete record coverage and no legacy fields;
#   - abc_structure --value-source executes record recovery, and the removed
#     legacy --source/--shared-source routes are rejected as usage errors.
# Run via `ctest --test-dir build -R '^source_route_cli_test$'`.
set(ABCD_ROUTE_HASH
  "854fb0d9bbc1df0e0e9b2ab4b0b5adedaf78831561dbe8692e323128f572ba9e")

# InspectError::kArguments is the first wire enum value in abc_semantic.cc.
set(ABCD_ARGUMENT_ERROR 0)
foreach(family_selector IN ITEMS --conditional-family --family-source-draft)
foreach(selector IN ITEMS --conditional-family --family-source-draft --conditional-values --value-source
    --record-value-slice --class-source --function-source --record-source
    --instructions --exception-flow --exception-template)
  foreach(first IN ITEMS 1 0)
    if(first)
      set(arguments "${family_selector}" "${selector}")
    else()
      set(arguments "${selector}" "${family_selector}")
    endif()
    execute_process(COMMAND "${ABCD_SEMANTIC}" "${ABCD_SAMPLE}.absent" "7366"
      ${arguments} RESULT_VARIABLE status OUTPUT_VARIABLE report ERROR_VARIABLE failure)
    string(JSON error GET "${failure}" error)
    if(status EQUAL 0 OR NOT report STREQUAL "" OR NOT error EQUAL ABCD_ARGUMENT_ERROR)
      message(FATAL_ERROR "family selector must refuse before file access: ${arguments}")
    endif()
  endforeach()
endforeach()
endforeach()

foreach(detail IN ITEMS --instructions --exception-flow --exception-template)
  foreach(first IN ITEMS 1 0)
    if(first)
      set(arguments "${detail}" "--value-source")
    else()
      set(arguments "--value-source" "${detail}")
    endif()
    execute_process(COMMAND "${ABCD_SEMANTIC}" "${ABCD_SAMPLE}" "7586"
      ${arguments} RESULT_VARIABLE status OUTPUT_VARIABLE report ERROR_QUIET)
    if(status EQUAL 0)
      message(FATAL_ERROR "prohibited combination accepted: ${arguments}")
    endif()
    string(FIND "${report}" "route_receipt" receipt_at)
    if(NOT receipt_at EQUAL -1)
      message(FATAL_ERROR "argument rejection must not emit a receipt")
    endif()
  endforeach()
endforeach()

foreach(arguments IN ITEMS
    "--function-source;--record-source" "--record-source;--class-source"
    "--value-source;--value-source" "--function-source;--instructions")
  execute_process(COMMAND "${ABCD_SEMANTIC}" "${ABCD_SAMPLE}" "7586" ${arguments}
    RESULT_VARIABLE status OUTPUT_VARIABLE report ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "conflicting options accepted: ${arguments}")
  endif()
  string(FIND "${report}" "route_receipt" receipt_at)
  if(NOT receipt_at EQUAL -1)
    message(FATAL_ERROR "argument rejection must not emit a receipt")
  endif()
endforeach()

# T03 prints this method in either option order without entering the legacy
# writer. A class-member fragment is still not a complete executable record.
foreach(arguments IN ITEMS "--function-source;--value-source"
                            "--value-source;--function-source")
  execute_process(COMMAND "${ABCD_SEMANTIC}" "${ABCD_SAMPLE}" "7586" ${arguments}
    RESULT_VARIABLE status OUTPUT_VARIABLE report ERROR_QUIET)
  if(status EQUAL 0)
    message(FATAL_ERROR "refused route must not exit 0: ${arguments}")
  endif()
  string(JSON route GET "${report}" route_receipt route)
  if(NOT route STREQUAL "value_tree")
    message(FATAL_ERROR "refusal lost its route: ${report}")
  endif()
  string(JSON stage GET "${report}" route_receipt stage)
  if(NOT stage STREQUAL "printed")
    message(FATAL_ERROR "unexpected import stage: ${report}")
  endif()
  string(JSON coverage GET "${report}" route_receipt coverage)
  if(NOT coverage STREQUAL "complete")
    message(FATAL_ERROR "unexpected import coverage: ${report}")
  endif()
  string(JSON writer ERROR_VARIABLE writer_absent GET "${report}" route_receipt legacy_writer_used)
  if(NOT writer_absent)
    message(FATAL_ERROR "receipt still carries the removed legacy writer field: ${report}")
  endif()
  string(JSON failure_type TYPE "${report}" route_receipt failure)
  string(JSON revision GET "${report}" route_receipt work_revision)
  if(NOT failure_type STREQUAL "NULL" OR NOT revision EQUAL 1)
    message(FATAL_ERROR "import lost its revision or claims failure: ${report}")
  endif()
  string(JSON sha256 GET "${report}" route_receipt identity sha256)
  if(NOT sha256 STREQUAL "${ABCD_ROUTE_HASH}")
    message(FATAL_ERROR "receipt identity is not bound to the input: ${report}")
  endif()
  string(JSON method GET "${report}" route_receipt method)
  string(JSON record GET "${report}" route_receipt record)
  if(NOT method EQUAL 7586 OR NOT record EQUAL 6897)
    message(FATAL_ERROR "receipt identity lost its entities: ${report}")
  endif()
  string(JSON fragment GET "${report}" fragment)
  string(JSON complete GET "${report}" method_slice_complete)
  string(JSON standalone GET "${report}" executable)
  string(JSON text GET "${report}" source)
  string(JSON unit GET "${report}" positions unit)
  if(NOT fragment STREQUAL "class_member" OR NOT complete OR standalone
      OR text STREQUAL "" OR NOT unit STREQUAL "utf8_byte")
    message(FATAL_ERROR "invalid class-member slice: ${report}")
  endif()
endforeach()

# The value route has no class-constructor fragment view: the removed
# --class-source form is a usage error, never a fabricated receipt.
execute_process(COMMAND "${ABCD_SEMANTIC}" "${ABCD_SAMPLE}" "7586"
  --class-source --value-source RESULT_VARIABLE status OUTPUT_VARIABLE report ERROR_VARIABLE failure)
string(JSON error GET "${failure}" error)
if(status EQUAL 0 OR NOT report STREQUAL "" OR NOT error EQUAL ABCD_ARGUMENT_ERROR)
  message(FATAL_ERROR "removed --class-source must be an argument error: ${report}")
endif()
# --record-source prints the owning record through the record route, never
# the method fragment. Positive: record 6897 (owner of method 7586) is
# completely recovered on the value-tree route.
execute_process(COMMAND "${ABCD_SEMANTIC}" "${ABCD_SAMPLE}" "7586"
  --record-source --value-source RESULT_VARIABLE status OUTPUT_VARIABLE report ERROR_QUIET)
string(JSON record GET "${report}" record)
string(JSON route GET "${report}" route)
string(JSON complete GET "${report}" source_complete)
string(JSON text GET "${report}" source)
if(NOT status EQUAL 0 OR NOT record EQUAL 6897 OR NOT route STREQUAL "value_tree"
    OR NOT complete OR text STREQUAL "")
  message(FATAL_ERROR "record 6897 must be completely recovered: ${report}")
endif()
string(FIND "${text}" "INCOMPLETE" incomplete_at)
string(FIND "${text}" "UNRECOVERED" unrecovered_at)
if(NOT incomplete_at EQUAL -1 OR NOT unrecovered_at EQUAL -1)
  message(FATAL_ERROR "complete record carries gap markers: ${report}")
endif()
# Negative: record 4767 (owner of method 4917) keeps an unsupported
# constructor operation. It is reported incomplete, with explicit typed gap
# markers in the partial text, never as complete source.
execute_process(COMMAND "${ABCD_SEMANTIC}" "${ABCD_SAMPLE}" "4917"
  --record-source --value-source RESULT_VARIABLE status OUTPUT_VARIABLE report ERROR_QUIET)
string(JSON record GET "${report}" record)
string(JSON complete GET "${report}" source_complete)
string(JSON executable GET "${report}" executable)
string(JSON text GET "${report}" source)
string(FIND "${text}" "/* INCOMPLETE: partial decompilation; not executable-equivalent. */" banner_at)
string(FIND "${text}" "/* UNRECOVERED: unsupported operation; PC " reason_at)
if(NOT record EQUAL 4767 OR complete OR executable OR banner_at EQUAL -1 OR reason_at EQUAL -1)
  message(FATAL_ERROR "incomplete record lost its typed gap: ${report}")
endif()

# A bare --value-source selects the function form; it never prints the
# instruction summary.
execute_process(COMMAND "${ABCD_SEMANTIC}" "${ABCD_SAMPLE}" "7586"
  "--value-source" RESULT_VARIABLE status OUTPUT_VARIABLE report ERROR_QUIET)
if(status EQUAL 0)
  message(FATAL_ERROR "refused summary must not exit 0")
endif()
string(FIND "${report}" "route_receipt" receipt_at)
if(receipt_at EQUAL -1)
  message(FATAL_ERROR "summary mode lost its receipt: ${report}")
endif()
foreach(legacy_field IN ITEMS "\"ssa_values\"" "\"instructions\"")
  string(FIND "${report}" "${legacy_field}" field_at)
  if(NOT field_at EQUAL -1)
    message(FATAL_ERROR "summary ran instead of the value route: ${report}")
  endif()
endforeach()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef nonce)
set(root "${CMAKE_CURRENT_BINARY_DIR}/source_route_cli_${nonce}")
execute_process(COMMAND "${ABCD_STRUCTURE}" "${ABCD_SAMPLE}" "${root}"
  "--value-source" RESULT_VARIABLE status OUTPUT_VARIABLE report ERROR_QUIET)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "value-route export failed: ${report}")
endif()
file(READ "${root}/manifest.json" manifest)
string(JSON view GET "${manifest}" view)
if(NOT view STREQUAL "declarations_with_value_source")
  message(FATAL_ERROR "unexpected value-route view: ${manifest}")
endif()
string(JSON sha256 GET "${manifest}" sha256)
if(NOT sha256 STREQUAL "${ABCD_ROUTE_HASH}")
  message(FATAL_ERROR "manifest identity mismatch: ${manifest}")
endif()
string(JSON count LENGTH "${manifest}" records)
if(count EQUAL 0)
  message(FATAL_ERROR "value-route export lost its records")
endif()
math(EXPR last "${count} - 1")
foreach(index RANGE ${last})
  string(JSON source_status GET "${manifest}" records ${index} source_status)
  string(JSON route GET "${manifest}" records ${index} source_route)
  if(NOT route STREQUAL "value_tree" OR source_status EQUAL 5)
    message(FATAL_ERROR "record ${index} did not run the actual value route")
  endif()
  string(JSON source_path ERROR_VARIABLE missing_source GET "${manifest}" records ${index} source)
  if(source_status EQUAL 0)
    string(JSON complete GET "${manifest}" records ${index} source_complete)
    string(JSON executable GET "${manifest}" records ${index} executable)
    if(missing_source OR NOT complete OR executable OR NOT EXISTS "${root}/${source_path}")
      message(FATAL_ERROR "record ${index} has inconsistent recovery output")
    endif()
  elseif(NOT missing_source)
    message(FATAL_ERROR "failed record ${index} published source")
  endif()
endforeach()
if(NOT count EQUAL 19)
  message(FATAL_ERROR "expected 19 records, saw ${count}")
endif()
file(REMOVE_RECURSE "${root}")

# The removed legacy routes are usage errors and publish nothing.
foreach(legacy_option IN ITEMS --source --shared-source)
  execute_process(COMMAND "${ABCD_STRUCTURE}" "${ABCD_SAMPLE}" "${root}"
    "${legacy_option}" RESULT_VARIABLE status OUTPUT_VARIABLE report ERROR_QUIET)
  if(status EQUAL 0 OR EXISTS "${root}")
    message(FATAL_ERROR "removed legacy route ${legacy_option} was accepted: ${report}")
  endif()
endforeach()
