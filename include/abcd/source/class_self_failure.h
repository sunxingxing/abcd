#ifndef ABCD_SOURCE_CLASS_SELF_FAILURE_H_
#define ABCD_SOURCE_CLASS_SELF_FAILURE_H_
#include "abcd/source/tree_failure.h"
namespace abcd::source {
// Append-only. A recovery gate is evidence of preparation, never publication.
enum class ClassSelfInitializationError : std::uint8_t {
  kOccurrence,
  kStorage,
  kInitialization,
  kWrite,
  kGuard,
  kScope,
  kName,
  kOrder,
  kCatchState,
  kCorrespondence,
  kResourceLimit,
  kWorkLimit,
  kPreparationGate,
  kRecoveryGate
};
struct ClassSelfInitializationFailure {
  ClassSelfInitializationError kind;
  TreeLocation location;
  abc::EntityOffset creator{}, initializer{};
  std::optional<analysis::OpId> creation, allocation, store, read, guard;
  std::optional<analysis::LexicalSlot> slot;
  std::optional<analysis::UseId> use;
  enum class Projection : std::uint8_t {
    kCaptureEnclosure,
    kClassInitializationPorts,
    kClassCompletion,
    kFamilyEnclosure,
    kSyntax,
    kNameResolution,
    kGuard,
    kWrite,
    kCleanup,
    kMemberAbi
  };
  std::optional<Projection> projection;
  struct AbiLengths {
    abc::EntityOffset method;
    std::uint32_t declared, signature;
  };
  std::optional<AbiLengths> abi_lengths;
};
enum class ClassSelfInitializationStatus : std::uint8_t { kMissing, kVerified };
// This is a storage-bound scope VIEW, not a ClassTree/recovery receipt. A
// verified view must not be confused with admission of a captured initializer.
struct ClassSelfScopeDiagnostic {
  ClassSelfInitializationStatus status = ClassSelfInitializationStatus::kMissing;
  std::uint32_t owners = 0, references = 0, inner = 0, outer = 0,
                public_commits = 0;
  std::optional<ClassSelfInitializationFailure> failure;
};
struct ClassSelfInitializationDiagnostic {
  ClassSelfInitializationStatus status =
      ClassSelfInitializationStatus::kMissing;
  abc::EntityOffset creator{}, initializer{};
  analysis::OpId creation{}, allocation{}, store{};
  analysis::LexicalSlot slot{};
  std::uint32_t reads = 0, captures = 0, cleanup = 0, families = 0;
  std::optional<ClassSelfInitializationFailure> failure;
  std::optional<ClassSelfScopeDiagnostic> scope_view;
  std::optional<ClassSelfInitializationFailure> recovery;
  std::uint32_t tree_references = 0, syntax_owners = 0;
  std::uint32_t bound_outer = 0;
};
}  // namespace abcd::source
#endif
