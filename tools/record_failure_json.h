#ifndef ABCD_TOOLS_RECORD_FAILURE_JSON_H_
#define ABCD_TOOLS_RECORD_FAILURE_JSON_H_

#include <nlohmann/json.hpp>

#include "abcd/source/record_source.h"

namespace abcd::tools {
// Manifest (schema 2) fields for a record whose value-tree route produced no
// source. The explicit source_failure_domain names the typed failure family;
// it never exposes a result-variant index whose meaning changes with the
// variant layout. PrintedSource is not a failure and writes nothing.
//   route_refusal:  source_failure {class, reason[, pc], causes, causes_truncated}
//   record_program: source_error (RecordProgramError), source_pc,
//                   source_causes, source_causes_truncated
// causes are typed {domain, value} entries, outermost stage first; the last
// entry is the leaf used for capability (Pareto) aggregation.
void DescribeRecordFailure(const source::RecordSourceResult& result,
                           nlohmann::json& record);
// Process-level failures stop the export: any outcome once a real
// allocation failure has been observed (sticky, tools/process_limit.h), and
// input I/O, input change or entry allocation failure. Without an observed
// allocation failure a resource limit (kResourceLimit, kMemoryLimit refusal)
// is this record's own budget and, like kRecordBudget, kOutputLimit and other
// typed refusals, is isolated to the record.
[[nodiscard]] bool RecordFailureIsFatal(const source::RecordSourceResult& result,
                                        bool allocation_failed);
}  // namespace abcd::tools

#endif  // ABCD_TOOLS_RECORD_FAILURE_JSON_H_
