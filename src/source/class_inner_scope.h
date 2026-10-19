#ifndef ABCD_SOURCE_CLASS_INNER_SCOPE_H_
#define ABCD_SOURCE_CLASS_INNER_SCOPE_H_
#include "class_self_scope.h"
#include "tree_checks.h"

namespace abcd::source {
// A sealed lexical scope, not permission to publish a class. Creation/cleanup
// and public publication still require the creator's joint port relation.
class ClassInnerScopeProof {
 public:
  ClassInnerScopeProof(ClassInnerScopeProof&&) noexcept = default;
  static std::variant<std::shared_ptr<const ClassInnerScopeProof>,
                      ClassSelfInitializationFailure>
  Build(const ClassSelfInitializationInput&, const ClassSelfInitialization&,
        analysis::SemanticBudget = {});
  const ClassSelfScope& Scope() const { return scope_.Evidence(); }
  ClassSelfInitializationCost Cost() const { return cost_; }
  RouteInputIdentity Identity() const { return identity_; }
  abc::EntityOffset Creator() const { return creator_; }
  abc::EntityOffset Initializer() const { return owner_.method; }
  analysis::IrRevision CreatorRevision() const { return creator_revision_; }
  analysis::CfgRevision CreatorControlRevision() const {
    return creator_control_;
  }
  void Verify(const FunctionTree&, TreeCheckBudget&) const;

 private:
  struct Read {
    analysis::InstructionId read, guard;
  };
  explicit ClassInnerScopeProof(ClassSelfScopeProof proof)
      : scope_(std::move(proof)) {}
  ClassSelfScopeProof scope_;
  RouteInputIdentity identity_;
  ClassSelfFamilyStamp owner_;
  abc::EntityOffset creator_;
  analysis::IrRevision creator_revision_;
  analysis::CfgRevision creator_control_;
  std::size_t site_count_ = 0;
  std::vector<Read> reads_;
  ClassSelfInitializationCost cost_;
};
void VerifyClassInnerScope(const FunctionTree&, TreeCheckBudget&);
}  // namespace abcd::source
#endif
