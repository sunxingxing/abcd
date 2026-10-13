#ifndef ABCD_SOURCE_RECORD_SOURCE_H_
#define ABCD_SOURCE_RECORD_SOURCE_H_

#include "abcd/source/record_module_context.h"
#include "abcd/analysis/method_roles.h"
#include "abcd/analysis/function_creation_sites.h"
#include "abcd/analysis/class_method_sites.h"
#include "abcd/source/record_program.h"
#include "abcd/source/printed_source.h"
#include "abcd/source/source_route.h"

namespace abcd::source {
struct RecordPrintDiagnostics {
  std::size_t reservation=0, printer_scratch=0, available=0;
  std::size_t counted_bytes=0, counted_peak=0;
  std::size_t methods=0, expressions=0, statements=0, operations=0, anchors=0;
  bool reached_print=false, count_complete=false;
  std::optional<TreePrintFailure> count_failure;
};
// Result of the value-tree record route: printed source, a typed record-level
// failure, or a typed route refusal from a recovery stage.
using RecordSourceResult = std::variant<PrintedSource, RecordProgramFailure, RouteRefusal>;
// Value-tree record source: the single production route. The record entry
// checks merged record flags and the unique func_main_0 entry, then builds the
// FunctionTree family for the whole record. Unsupported input is a typed
// failure or RouteRefusal; there is no alternative writer to fall back to.
// Context proves same-file module ownership. Metadata/lookup/roles/context are
// caller-owned; budget covers decoded body, exports and analysis. Output has
// its separate allowance. The caller must stage publication and recheck file.
// Creation indexes are built once per file and borrowed across records; a
// record containing classes or created functions requires them.
[[nodiscard]] RecordSourceResult BuildRecordSource(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const abc::RecordInfo& record, const RecordModuleContext& context,
    const analysis::MethodRoleIndex& roles, RecordProgramBudget budget,
    const RouteInputIdentity& identity,
    const analysis::FunctionCreationSites* functions = nullptr,
    const analysis::ClassMethodSites* classes = nullptr,
    RecordPrintDiagnostics* diagnostics = nullptr);
}  // namespace abcd::source
#endif
