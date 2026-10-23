#ifndef ABCD_SOURCE_FAMILY_CREATION_FAILURE_H_
#define ABCD_SOURCE_FAMILY_CREATION_FAILURE_H_
#include "abcd/source/tree_failure.h"
#include "abcd/abc/pool_reference.h"
namespace abcd::source {
// Diagnostic checks, not permission to treat a class barrier as a function.
// Pool slots are scoped by location.method; they are never entity offsets.
enum class FamilyCreationCheck : std::uint8_t {
  kBodySite, kSemanticSite, kProjectionSite, kProjectionOperation,
  kProjectionLive, kProjectionOwner, kProjectionKind, kPhasedIndex,
  kPhasedSite, kFunctionBinding, kCaptureFlow, kChildOrder, kSelfInitializer,
  kClassPreparationInput, kClassPreparationGate,
  kClassCaptureRequirements, kClassDeferredDeclaration, kClassHeritage,
  kClassStaticInitialization, kClassNestedPreparation, kClassCompletion,
  kLiteralMethod
};
enum class FamilyCreationKind : std::uint8_t { kFunction, kMethod, kClass, kOther };
struct FamilyOccurrenceId {
  std::uint32_t value;
  bool operator==(const FamilyOccurrenceId&) const = default;
};
struct FamilyCreationEvidence {
  FamilyCreationCheck reason;
  TreeLocation location;
  std::optional<analysis::InstructionId> instruction;
  std::optional<abc::EntityOffset> root;
  std::optional<FamilyOccurrenceId> occurrence;
  std::optional<FamilyCreationKind> kind;
  std::optional<abc::PoolIndex> constructor, literal;
  std::optional<analysis::OpId> projected_class;
  // The incoming creation of this descendant, separate from its failing site.
  std::optional<TreeLocation> descendant_source;
  bool operator==(const FamilyCreationEvidence&) const = default;
};
} // namespace abcd::source
#endif
