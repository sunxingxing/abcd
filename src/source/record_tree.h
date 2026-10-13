#ifndef ABCD_SOURCE_RECORD_TREE_H_
#define ABCD_SOURCE_RECORD_TREE_H_
#include "abcd/source/record_source.h"
namespace abcd::source {
// Caller has checked the merged record flags and unique func_main_0 entry.
RecordSourceResult BuildRecordTree(const corpus::AbcFile&, const abc::Header&,
    const abc::MetadataIndex&, const abc::RecordInfo&, const RecordModuleContext&,
    abc::MethodPosition, RouteInputIdentity, RecordProgramBudget, const abc::MethodLookup&,
    const analysis::MethodRoleIndex&, const analysis::FunctionCreationSites*, const analysis::ClassMethodSites*, RecordPrintDiagnostics* = nullptr);
}
#endif
