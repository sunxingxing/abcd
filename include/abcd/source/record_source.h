#ifndef ABCD_SOURCE_RECORD_SOURCE_H_
#define ABCD_SOURCE_RECORD_SOURCE_H_

#include "abcd/source/record_module_context.h"
#include "abcd/source/record_program.h"
#include "abcd/source/semantic_entry.h"

namespace abcd::source {
using RecordSourceResult = std::variant<PrintedSource, RecordProgramFailure,
    analysis::SemanticFailure, analysis::SemanticProofFailure,
    analysis::RegionFailure, SourceFailure>;
// Shared-pipeline artifact, not a complete/equivalent-source certificate.
// Context proves same-file module ownership. Metadata/lookup/roles/context are
// caller-owned; budget covers decoded body, exports and analysis. Output has
// its separate allowance. The caller must stage publication and recheck file.
// Optional realm_binding is an explicit embedding-host trust capability; it
// is never fabricated from an import, free identifier or mutable property.
[[nodiscard]] RecordSourceResult BuildRecordSource(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const abc::RecordInfo& record, const RecordModuleContext& context,
    const analysis::MethodRoleIndex& roles, RecordProgramBudget budget = {},
    const HostRealmBinding* realm_binding = nullptr);
}  // namespace abcd::source
#endif
