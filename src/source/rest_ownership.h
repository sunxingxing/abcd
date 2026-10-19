#ifndef ABCD_SOURCE_REST_OWNERSHIP_H_
#define ABCD_SOURCE_REST_OWNERSHIP_H_
#include "abcd/analysis/value_environment.h"
#include "abcd/analysis/value_ir.h"
namespace abcd::source {
enum class RestOwnershipError : std::uint8_t { kFrame, kAllocation, kArgumentUse, kOrder, kResourceLimit, kWorkLimit };
enum class RestDeclarationRole : std::uint8_t {
  kEnvironment,
  kFunction,
  kCellStore
};
struct RestDeclarationEvent {
  analysis::OpId operation;
  RestDeclarationRole role;
  bool operator==(const RestDeclarationEvent&) const = default;
};
struct RestOwnershipFailure {
  RestOwnershipError kind = RestOwnershipError::kOrder;
  std::optional<analysis::OpId> operation;
  std::optional<abc::CodeOffset> pc;
};
struct RestOwnership {
  std::size_t allocated_bytes;
  std::uint64_t work;
  std::vector<RestDeclarationEvent> declarations;
};
// Verified normal or phased IR and its original body/frame. Native rest hoists
// one array allocation only across unobservable argument transport or verified
// private declaration instantiation. Residual declaration/capture order is
// unchanged. Original argument scalar uses, slot overwrites, snapshots and
// repeated allocation stay distinct. The optional environment owner is already
// verified for this graph and remains alive/charged through the call. It is
// required only for declaration prefixes. No register numbering, prologue
// length or instruction-adjacency template.
[[nodiscard]] std::variant<RestOwnership, RestOwnershipError>
ProveRestOwnership(const analysis::ValueProgram&, const abc::MethodBody&,
                   const abc::FrameLayout&, std::uint16_t first,
                   analysis::SemanticBudget = {},
                   RestOwnershipFailure* = nullptr,
                   const analysis::ValueEnvironments* = nullptr);
// Re-derive declaration membership, storage and frame ownership from immutable
// inputs, not the candidate event labels. Scratch is bounded by the same proof.
[[nodiscard]] std::variant<RestOwnership, RestOwnershipError>
VerifyRestOwnership(const analysis::ValueProgram&, const abc::MethodBody&,
                    const abc::FrameLayout&, std::uint16_t first,
                    const RestOwnership&, analysis::SemanticBudget = {},
                    const analysis::ValueEnvironments* = nullptr);
}
#endif
