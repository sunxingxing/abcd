#ifndef ABCD_SOURCE_CLASS_SELF_DEFERRED_H_
#define ABCD_SOURCE_CLASS_SELF_DEFERRED_H_
#include "class_tree.h"
#include "family_file.h"

namespace abcd::source {
// The early reader has no creator syntax. Retain its sealed ORIGINAL inputs
// until the common class preparation has the actual CaptureTree. Retained
// families are raw-analysis snapshots only: their early module-parent inputs
// must not be dereferenced or passed to PreparedFamily::Finish after a creator
// moves. Finishing still uses freshly prepared families under the real creator.
struct ClassSelfDeferred {
  FunctionContext context;
  FileFamily initializer_family;
  ClassPreparation members;
  ClassSelfInitializationProof initialization;
  ClassSelfScopeProof scope;
  ClassInitializerTreeDraft initializer;
  EvaluationPlan evaluation;
  ClassSelfInitializationDiagnostic diagnostic;
  std::vector<ClassSelfOuterBinding> bindings;
  std::optional<TreeFlow> flow;
  ClassSelfInitializationCost enclosure_cost;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
std::variant<ClassSelfInitializationCost, ClassSelfInitializationFailure>
VerifyDeferredClassSelf(const corpus::AbcFile&, const abc::Header&,
                        const abc::MetadataIndex&, const abc::MethodLookup&,
                        const analysis::MethodRoleIndex&,
                        const analysis::ClassMethodSites&, const FileFunction&,
                        const ClassSelfDeferred&,
                        analysis::SemanticBudget = {});
std::variant<ClassSelfInitializationCost, ClassSelfInitializationFailure>
VerifyClassSelfEnclosure(const corpus::AbcFile&, const abc::Header&,
                         const abc::MetadataIndex&, const abc::MethodLookup&,
                         const analysis::MethodRoleIndex&,
                         const analysis::ClassMethodSites&, const FileFunction&,
                         const ClassSelfDeferred&, const ClassTree&,
                         FunctionOwnerId, CaptureTree, const CaptureStorage*,
                         analysis::SemanticBudget = {});
// Still returns a typed gate if joint initialization/recovery is unproved.
ClassSelfInitializationDiagnostic BindDeferredClassSelf(
    const corpus::AbcFile&, const abc::Header&, const abc::MetadataIndex&,
    const abc::MethodLookup&, const analysis::MethodRoleIndex&,
    const analysis::ClassMethodSites&, const FileFunction&, ClassSelfDeferred&,
    ClassTree&, FunctionOwnerId, CaptureTree, const CaptureStorage*,
    analysis::SemanticBudget = {}, const ModuleEnclosure* = nullptr);
}  // namespace abcd::source
#endif
