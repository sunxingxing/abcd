#ifndef ABCD_SOURCE_SCRIPT_TREE_H_
#define ABCD_SOURCE_SCRIPT_TREE_H_
#include "record_tree.h"
namespace abcd::source {
// Separate script entry: no ModuleEnclosure, imports, exports or module cells.
RecordSourceResult BuildScriptTree(
    const corpus::AbcFile&, const abc::Header&, const abc::MetadataIndex&,
    const abc::RecordInfo&, const RecordModuleContext&, abc::MethodPosition,
    RouteInputIdentity, RecordProgramBudget, const abc::MethodLookup&,
    const analysis::MethodRoleIndex&, const analysis::FunctionCreationSites*,
    const analysis::ClassMethodSites*);
}  // namespace abcd::source
#endif
