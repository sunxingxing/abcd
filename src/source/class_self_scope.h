#ifndef ABCD_SOURCE_CLASS_SELF_SCOPE_H_
#define ABCD_SOURCE_CLASS_SELF_SCOPE_H_

#include "class_self_initialization.h"

namespace abcd::source {
// A class scope is an occurrence plus ORIGINAL storage, never a fictional
// function owner or a CellId borrowed from a different plan. Spelling and tree
// enclosure are subsequent obligations; this view grants no printer admission.
struct ClassInnerBinding {
  CreatorClassOccurrence occurrence;
  analysis::OpId creation, allocation;
  analysis::LexicalSlot slot;
  bool operator==(const ClassInnerBinding& other) const {
    return occurrence == other.occurrence && creation == other.creation &&
           allocation == other.allocation && slot.value == other.slot.value;
  }
};
struct ClassSelfOwnerRange {
  std::uint32_t family, first, count;
  bool operator==(const ClassSelfOwnerRange&) const = default;
};
struct ClassInnerReference {
  ClassSelfFamilyStamp owner;
  IncomingCell incoming;
  analysis::OpId read, guard;
  analysis::ValueId value;
  std::optional<analysis::ValueHandler> exception;
  bool operator==(const ClassInnerReference& other) const {
    return owner == other.owner && incoming.depth == other.incoming.depth &&
           incoming.slot.value == other.incoming.slot.value &&
           read == other.read && guard == other.guard && value == other.value &&
           exception == other.exception;
  }
};
enum class ClassSelfRequirementKind : std::uint8_t { kClassInner, kOuter };
struct ClassSelfRequirement {
  ClassSelfFamilyStamp owner;
  IncomingCell incoming;
  analysis::OpId creation;
  ClassSelfRequirementKind kind;
  analysis::CaptureDomain domain;
  bool operator==(const ClassSelfRequirement& other) const {
    return owner == other.owner && incoming.depth == other.incoming.depth &&
           incoming.slot.value == other.incoming.slot.value &&
           creation == other.creation && kind == other.kind &&
           domain == other.domain;
  }
};
enum class ClassSelfPortKind : std::uint8_t {
  kClassAvailable,
  kInternalCommit,
  kInitializerBefore,
  kInitializerCommit,
  kPublicCommit
};
struct ClassSelfPort {
  ClassSelfPortKind kind;
  analysis::OpId operation;
  analysis::OriginId origin;
  std::optional<analysis::UseId> use;
  std::optional<analysis::ValueId> value;
  bool operator==(const ClassSelfPort&) const = default;
};
struct ClassSelfScope {
  ClassInnerBinding binding;
  // Relative, disjoint, full ranges to reserve before syntax owners exist.
  // They are NOT FunctionOwnerIds and cannot resolve a TreeCapture by number.
  std::vector<ClassSelfOwnerRange> ranges;
  std::vector<ClassSelfFamilyStamp> owners;
  std::vector<ClassSelfRequirement> requirements;
  std::vector<ClassInnerReference> references;
  std::vector<ClassSelfPort> ports;
  // Original member installation effects before the initializer. No call or
  // user effect may be hidden in this private corridor.
  std::vector<analysis::OpId> installations;
  std::vector<ClassSelfCleanup> cleanup;
  std::optional<analysis::DispatchTarget> initializer_exception;
  bool operator==(const ClassSelfScope&) const = default;
};
// Preparation evidence only. The source-tree ClassExpr/CaptureTree and event
// projection must still consume this view before a recovery gate is truthful.
class ClassSelfScopeProof {
 public:
  ClassSelfScopeProof(ClassSelfScopeProof&&) noexcept = default;
  ClassSelfScopeProof& operator=(ClassSelfScopeProof&&) noexcept = default;
  static std::variant<ClassSelfScopeProof, ClassSelfInitializationFailure>
  Build(const ClassSelfInitializationInput&, const ClassSelfInitialization&,
        analysis::SemanticBudget = {});
  const ClassSelfScope& Evidence() const { return evidence_; }
  const ClassSelfInitializationCost& Cost() const { return cost_; }
  // Unlisted owners and stale revisions retain the ordinary internal-value
  // refusal. This cannot resolve by method entity, spelling or CellId alone.
  const ClassInnerReference* FindReference(const ClassSelfFamilyStamp&,
                                           analysis::OpId read) const;

 private:
  ClassSelfScopeProof() = default;
  ClassSelfScope evidence_;
  ClassSelfInitializationCost cost_;
};
std::variant<ClassSelfInitializationCost, ClassSelfInitializationFailure>
VerifyClassSelfScope(const ClassSelfInitializationInput&,
                     const ClassSelfInitialization&, const ClassSelfScope&,
                     analysis::SemanticBudget = {});
}  // namespace abcd::source
#endif
