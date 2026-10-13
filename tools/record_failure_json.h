#ifndef ABCD_TOOLS_RECORD_FAILURE_JSON_H_
#define ABCD_TOOLS_RECORD_FAILURE_JSON_H_

#include <nlohmann/json.hpp>

#include "abcd/source/record_source.h"
#include "abcd/source/record_module_context.h"

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
void DescribeModuleContextFailure(const source::RecordModuleFailure&, nlohmann::json&);
// Process-level failures stop the export. They are decided out of band, not
// from the record's error code, because stages fold unrelated errors into
// kRead/kWrite (e.g. malformed name strings into kRead, invalid literals into
// kWrite of an in-memory stream). process_fault is the caller's observation
// of a sticky real allocation failure (tools/process_limit.h), a sticky input
// read failure (corpus::AbcFile::ReadFailed) or an input change; with it every
// outcome is fatal, including recovered success. Without it only an
// allocation failure caught at the record entry (kAllocationFailure) is fatal;
// every other typed failure, including budgets, is isolated to the record.
// Output file I/O is checked by the caller where the files are written.
[[nodiscard]] bool RecordFailureIsFatal(const source::RecordSourceResult& result,
                                        bool process_fault);
}  // namespace abcd::tools

#endif  // ABCD_TOOLS_RECORD_FAILURE_JSON_H_
