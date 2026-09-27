#ifndef ABCD_SOURCE_FAMILY_WRITES_H_
#define ABCD_SOURCE_FAMILY_WRITES_H_

#include "family_file.h"

namespace abcd::source {
enum class FamilyWriteError : std::uint8_t {
  kOwner, kCoverage, kDomain, kStore, kCapture, kResourceLimit, kWorkLimit
};
struct FamilyWriteFailure {
  FamilyWriteError kind;
  FamilyId member;
  std::optional<abc::CodeOffset> pc;
};
struct FamilyWrites {
  FamilyWriteFacts facts;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// Requires unchanged, complete file-family inputs and freshly verified semantic
// trees/default signatures/flows/capture layout. Rechecks every original store's
// Language domain and residual placement, every capture edge and every barrier.
// A closed root plus initialized rebasing and all Language stores closes the
// transitive write-domain premise by execution induction, NOT immutability.
// Does not authorize snapshots, eliminate writes, or close external source scope.
[[nodiscard]] std::variant<FamilyWrites, FamilyWriteFailure> VerifyFamilyWrites(
    const abc::MetadataIndex& index, const FileFamily& inputs,
    const ConstructedFamily& trees, analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
