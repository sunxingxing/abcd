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
};
enum class ClassSelfInitializationStatus : std::uint8_t { kMissing, kVerified };
struct ClassSelfInitializationDiagnostic {
  ClassSelfInitializationStatus status =
      ClassSelfInitializationStatus::kMissing;
  abc::EntityOffset creator{}, initializer{};
  analysis::OpId creation{}, allocation{}, store{};
  analysis::LexicalSlot slot{};
  std::uint32_t reads = 0, captures = 0, cleanup = 0, families = 0;
  std::optional<ClassSelfInitializationFailure> failure;
};
}  // namespace abcd::source
#endif
