#ifndef ABCD_TOOLS_RECORD_FAILURE_JSON_H_
#define ABCD_TOOLS_RECORD_FAILURE_JSON_H_

#include <nlohmann/json.hpp>

#include "abcd/source/record_source.h"

namespace abcd::tools {
// Manifest (schema 2) fields for a record whose value-tree route produced no
// source. The explicit source_failure_domain names the typed failure family;
// it never exposes a result-variant index whose meaning changes with the
// variant layout. PrintedSource is not a failure and writes nothing.
//   route_refusal:  source_failure {class, reason[, pc]}
//   record_program: source_error (RecordProgramError), source_pc
void DescribeRecordFailure(const source::RecordSourceResult& result,
                           nlohmann::json& record);
// Process-level failures stop the export: input I/O, input change, entry
// allocation failure and kResourceLimit, whose origin lower stages do not
// distinguish from a real allocation failure. Only a proven record-owned
// budget (kRecordBudget) or output allowance (kOutputLimit) failure and typed
// route refusals are isolated to the record.
[[nodiscard]] bool RecordFailureIsFatal(const source::RecordSourceResult& result);
}  // namespace abcd::tools

#endif  // ABCD_TOOLS_RECORD_FAILURE_JSON_H_
