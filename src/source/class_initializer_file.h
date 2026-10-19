#ifndef ABCD_SOURCE_CLASS_INITIALIZER_FILE_H_
#define ABCD_SOURCE_CLASS_INITIALIZER_FILE_H_
#include "function_file.h"
#include "function_context.h"
namespace abcd::source {
struct ClassInitializerFunction {
  FunctionContext context;
  FileFunction function;
};
using ClassInitializerReadFailure = std::variant<ClassInitializerError, CreationContextFailure,
    FunctionFileFailure, abc::MetadataError>;
// Resolve the genuine occurrence without loading a body. Conditional family
// evidence can use it without weakening or retrying the Closed reader below.
[[nodiscard]] std::variant<FunctionContext, ClassInitializerReadFailure> ResolveClassInitializerContext(
    const corpus::AbcFile&, const abc::Header&, const abc::MetadataIndex&,
    const abc::MethodLookup&, const analysis::MethodRoleIndex&,
    const analysis::FunctionCreationSites&, const FileFunction&, const ClassInitializerCall&,
    analysis::SemanticBudget = {});
// Fresh unchanged parent and same-file indexes. Bind the indexed call to the
// actual DEFINEMETHOD occurrence, then read its body through FunctionInput and
// FunctionValues with initialized dynamic this. No statement/body substitution.
// Own-field lowering and safe placement into class syntax are still required.
[[nodiscard]] std::variant<ClassInitializerFunction, ClassInitializerReadFailure> ReadClassInitializerFunction(
    const corpus::AbcFile&, const abc::Header&, const abc::MetadataIndex&,
    const abc::MethodLookup&, const analysis::MethodRoleIndex&,
    const analysis::FunctionCreationSites&, const FileFunction&, const ClassInitializerCall&,
    analysis::SemanticBudget = {}, const RecordModuleContext* modules = nullptr);
}
#endif
