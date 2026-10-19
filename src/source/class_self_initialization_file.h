#ifndef ABCD_SOURCE_CLASS_SELF_INITIALIZATION_FILE_H_
#define ABCD_SOURCE_CLASS_SELF_INITIALIZATION_FILE_H_
#include "class_self_initialization.h"
namespace abcd::source {
// Failure-only evidence. Never substitutes for the original refusal or grants
// a ClassSelfOrder, capture target, source scope, tree or publication
// permission.
ClassSelfInitializationDiagnostic DiagnoseClassSelfInitialization(
    const corpus::AbcFile&, const abc::Header&, const abc::MetadataIndex&,
    const abc::MethodLookup&, const analysis::MethodRoleIndex&,
    const analysis::FunctionCreationSites&, const analysis::ClassMethodSites&,
    const FileFunction&, const ClassInitializerCall&, CreatorClassOccurrence,
    analysis::SemanticBudget = {}, const RecordModuleContext* = nullptr);
// Production consumption of a sealed scope into actual initializer syntax and
// real creator storage. Success still requires enclosing/recovery projection;
// recovery identifies that missing projection, not the original internal value.
ClassSelfInitializationDiagnostic ConsumeClassSelfInitialization(
    const corpus::AbcFile&, const abc::Header&, const abc::MetadataIndex&,
    const abc::MethodLookup&, const analysis::MethodRoleIndex&,
    const analysis::FunctionCreationSites&, const analysis::ClassMethodSites&,
    FileFunction&, const ClassInitializerCall&, CreatorClassOccurrence,
    analysis::SemanticBudget = {}, const RecordModuleContext* = nullptr);
}  // namespace abcd::source
#endif
