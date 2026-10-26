#ifndef ABCD_ANALYSIS_ENVIRONMENT_RENEWAL_H_
#define ABCD_ANALYSIS_ENVIRONMENT_RENEWAL_H_

#include "abcd/analysis/exception_environment.h"
#include "abcd/analysis/exception_ssa.h"

namespace abcd::analysis {
enum class EnvironmentRenewalError : std::uint8_t {
  kInput,
  kTopology,
  kParent,
  kSlotMap,
  kCopyValue,
  kInterference,
  kActivation,
  kInitialization,
  kCapture,
  kException,
  kCoverage,
  kSourceScope,
  kResourceLimit,
  kWorkLimit
};
struct EnvironmentRenewalFailure {
  EnvironmentRenewalError kind;
  abc::CodeOffset pc{};
};
// The source owner supplies its existing resource-completion premise. Raw
// callers default to observable; no ordinary exceptional edge is excluded.
enum class EnvironmentRenewalResources : std::uint8_t {
  kObservable,
  kExcluded
};
struct EnvironmentTokenId {
  std::uint32_t value;
  auto operator<=>(const EnvironmentTokenId&) const = default;
};
enum class EnvironmentTokenKind : std::uint8_t { kEntry, kAllocation, kPhi };
struct EnvironmentToken {
  EnvironmentTokenKind kind;
  std::uint32_t site;
  std::uint32_t depth;
  std::optional<EnvironmentTokenId> parent;
  auto operator<=>(const EnvironmentToken&) const = default;
};
struct EnvironmentTokenInput {
  EnvironmentTokenId phi, value;
  SsaEdge edge;
};
struct EnvironmentTransfer {
  std::uint32_t event;
  std::uint16_t old_slot, new_slot;
  ExceptionNodeId read, store;
  SsaValueId saved;
  EnvironmentTokenId old_token, new_token;
};
struct EnvironmentRenewalEvent {
  ExceptionNodeId pop, allocation;
  EnvironmentTokenId old_token, new_token;
  EnvironmentId old_environment, new_environment;
};
struct EnvironmentReset {
  EnvironmentId family;
  std::uint16_t slot;
  ExceptionNodeId initializer;
};
struct EnvironmentExcludedResource {
  ExceptionNodeId source, target;
  auto operator<=>(const EnvironmentExcludedResource&) const = default;
};
enum class EnvironmentRenewalRole : std::uint8_t {
  kOrdinary,
  kAllocation,
  kDeparture,
  kTransferRead,
  kTransferStore
};
struct EnvironmentRenewal {
  std::vector<EnvironmentToken> tokens;
  std::vector<EnvironmentTokenInput> inputs;
  std::vector<std::optional<EnvironmentTokenId>> current;
  // Physical definitions remain distinct, including replacement allocations.
  std::vector<EnvironmentDefinition> definitions;
  std::vector<EnvironmentAllocation> allocations;
  std::vector<EnvironmentId> families;
  std::vector<EnvironmentRenewalEvent> events;
  std::vector<EnvironmentTransfer> transfers;
  std::vector<EnvironmentReset> resets;
  std::vector<std::optional<std::uint32_t>> initializers;
  std::vector<EnvironmentExcludedResource> excluded;
  std::vector<EnvironmentRenewalRole> coverage;  // instruction-indexed
  EnvironmentRenewalResources resources;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
struct EnvironmentRenewalEvidence {
  std::shared_ptr<const ExceptionFlow> flow;
  std::shared_ptr<const ExceptionSsa> ssa;
  EnvironmentRenewal proof;
};
std::variant<std::uint64_t, EnvironmentRenewalFailure>
VerifyRenewedEnvironmentFlow(const EnvironmentRenewalEvidence&,
                             const EnvironmentFlow&, SemanticBudget = {});
std::variant<std::uint64_t, EnvironmentRenewalFailure>
VerifyEnvironmentRenewalEvidence(const EnvironmentRenewalEvidence&,
                                 SemanticBudget = {});
// Separate, legacy-refusal-only analysis. Environment SSA is built using the
// common sparse IDF/rename owner; phi inputs retain their actual phase edges.
// Body-reset projection is admitted only after activation/closed-use proof.
// Header-copy projection is deliberately a typed kSourceScope refusal.
// O((N+E+U+K) log(N+K+1)), K = actual transfer/phi/capture incidences.
std::variant<EnvironmentRenewal, EnvironmentRenewalFailure>
ProveEnvironmentRenewal(
    const ExceptionFlow&, const ExceptionSsa&,
    EnvironmentRenewalResources = EnvironmentRenewalResources::kObservable,
    SemanticBudget = {});
std::variant<std::uint64_t, EnvironmentRenewalFailure> VerifyEnvironmentRenewal(
    const ExceptionFlow&, const ExceptionSsa&, const EnvironmentRenewal&,
    SemanticBudget = {});
// Source binding correspondence, not physical allocation equality. Callers
// retain/reverify the token certificate for every projected access/capture.
std::variant<EnvironmentFlow, EnvironmentRenewalFailure>
ProjectRenewedEnvironments(const ExceptionFlow&, const ExceptionSsa&,
                           const EnvironmentRenewal&, SemanticBudget = {});
}  // namespace abcd::analysis
#endif
