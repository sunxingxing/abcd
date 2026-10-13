#ifndef ABCD_SOURCE_FAMILY_EMISSION_H_
#define ABCD_SOURCE_FAMILY_EMISSION_H_

#include "prepared_family.h"
#include "abcd/source/function_emission.h"

namespace abcd::source {
enum class FamilyEmitError : std::uint8_t { kReceipt, kPositions, kResourceLimit, kWorkLimit };
using FamilyEmitFailure = std::variant<FamilyEmitError, FamilyPrepareFailure, TreePrintFailure>;
// Owns the immutable family and its emitted positions. Receipts identify each
// creation occurrence by member order; repeated methods are not coalesced.
// Complete METHOD coverage is not class/record completion or test acceptance.
// Caller supplies the enclosing grammar/strictness/global binding contract.
// Positions remain relative to Destination().begin; transport is one offset.
// Failure may leave partial output: discard it and publish no receipts.
class FamilyEmission {
 public:
  FamilyEmission(FamilyEmission&&) noexcept = default;
  FamilyEmission& operator=(FamilyEmission&&) noexcept = default;
  [[nodiscard]] static std::variant<FamilyEmission, FamilyEmitFailure> Write(
      PreparedFamily family, FamilyDestination context, std::ostream& output,
      GeneratedOffset destination, EmitBudget limit = {}, analysis::SemanticBudget scratch = {});
  const PreparedFamily& Family() const { return family_; }
  const PrintedFamilyDraft& Positions() const { return positions_; }
  std::span<const MethodRouteReceipt> Receipts() const { return receipts_; }
  GeneratedRange Destination() const { return destination_; }
  std::size_t AllocatedBytes() const { return allocated_bytes_; }
 private:
  FamilyEmission(PreparedFamily family, PrintedFamilyDraft positions,
      std::vector<MethodRouteReceipt> receipts, GeneratedRange destination, std::size_t bytes)
      : family_(std::move(family)), positions_(std::move(positions)), receipts_(std::move(receipts)),
        destination_(destination), allocated_bytes_(bytes) {}
  PreparedFamily family_;
  PrintedFamilyDraft positions_;
  std::vector<MethodRouteReceipt> receipts_;
  GeneratedRange destination_;
  std::size_t allocated_bytes_;
};
}  // namespace abcd::source
#endif
