#ifndef ABCD_SOURCE_RECORD_SOURCE_H_
#define ABCD_SOURCE_RECORD_SOURCE_H_

#include "abcd/source/record_module_context.h"
#include "abcd/source/record_program.h"
#include "abcd/source/semantic_entry.h"

namespace abcd::source {
struct ClassMethodSelection;
struct FunctionCreationSelection;
// RouteRefusal is appended (stable indices). Also reports an explicit member
// migration obligation that could not be retained by record assembly.
using RecordSourceResult = std::variant<PrintedSource, RecordProgramFailure,
    analysis::SemanticFailure, analysis::SemanticProofFailure,
    analysis::RegionFailure, SourceFailure, RouteRefusal>;
// Shared-pipeline artifact, not a complete/equivalent-source certificate.
// Context proves same-file module ownership. Metadata/lookup/roles/context are
// caller-owned; budget covers decoded body, exports and analysis. Output has
// its separate allowance. The caller must stage publication and recheck file.
// Optional realm_binding is an explicit embedding-host trust capability; it
// is never fabricated from an import, free identifier or mutable property.
// policy has no default (invocation-context rule in source_route.h): the
// required value-tree route uses the merged record entry and same-file identity
// supplied below. Missing identity refuses before analysis. Neither case falls
// back to the legacy writer. Current module-tree support is straight-line and
// has no imported bindings or child functions; unsupported input stays explicit.
// Optional selection requires that exact class occurrence to use the verified
// value-tree member route. Other code remains legacy; this mixed artifact is
// not a complete value-tree record. A missing selected segment refuses output.
// Alternatively select a DEFINEFUNC/DEFINEMETHOD occurrence with the same
// obligation. Supplying both selections is rejected, never precedence-based.
[[nodiscard]] RecordSourceResult BuildRecordSource(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const abc::RecordInfo& record, const RecordModuleContext& context,
    const analysis::MethodRoleIndex& roles, RecordProgramBudget budget,
    const HostRealmBinding* realm_binding, RoutePolicy policy,
    const ClassMethodSelection* selection = nullptr,
    const FunctionCreationSelection* function_selection = nullptr,
    const RouteInputIdentity* identity = nullptr);
}  // namespace abcd::source
#endif
