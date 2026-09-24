#ifndef ABCD_ANALYSIS_EXCEPTION_LOCALS_H_
#define ABCD_ANALYSIS_EXCEPTION_LOCALS_H_

#include "abcd/analysis/exception_environment.h"
#include "abcd/analysis/capture_effect.h"
#include "abcd/analysis/exception_value_flow.h"
#include "abcd/analysis/semantic_validation.h"

namespace abcd::analysis {
struct ExceptionCaptureScope {
  LocalCellId cell;
  std::uint32_t begin;
  std::uint32_t end;
};
struct ExceptionLocals {
  LocalAccessPlan plan;  // Original instruction coordinates for the AST.
  std::vector<ExceptionLoadFact> loads;  // Normal Commit value premises.
  // Complete phase-graph intervals for initialized, closure-write-safe cells.
  // Conditional on all stores passing VerifyExceptionInputs. Empty when
  // re-entry could allocate multiple runtime cells at one source binding.
  std::vector<ExceptionCaptureScope> capture_scopes;
  std::vector<std::uint32_t> capture_entries;  // Indexed by phase node.
  std::size_t AllocatedBytes() const {
    return plan.AllocatedBytes() + loads.capacity() * sizeof(ExceptionLoadFact) +
           capture_scopes.capacity() * sizeof(ExceptionCaptureScope) +
           capture_entries.capacity() * sizeof(std::uint32_t);
  }
};
enum class ExceptionLocalError : std::uint8_t {
  kInvalidEnvironment, kUninitialized, kCapturedEnvironment, kResourceLimit
};
struct ExceptionLocalFailure {
  ExceptionLocalError kind;
  abc::CodeOffset pc;
};

// Same unchanged flow/environment analysis. Conditional proof: after applying
// loads to value propagation, EVERY store must pass VerifyExceptionInputs.
// Allocation dominates a completed store, whose subtree covers each load.
// Capture-capable creation marks its current local environment and ancestors
// escaped unless an explicit closure-write proof supplies an exemption.
// Sorted same-body capture-preservation certificates exempt only their exact
// creation PCs. They do not prove initialization or change exceptional edges.
// No normal-only dominance, default undefined, or branch-union initialization.
// Returned plan has no normal-CFG capture-initialization certificate.
// capture_scopes instead retain phase-graph initialization for forward-only
// closure-write-safe cells; owners must validate all stores before binding.
// O((N+E)log N + L log L + I + A) time; bounded O(N+E+I+A+L) space,
// for phase nodes/edges, lexical accesses, instructions and environments.
[[nodiscard]] std::variant<ExceptionLocals, ExceptionLocalFailure>
ProveExceptionLocals(const ExceptionFlow& flow, const EnvironmentFlow& environments,
                     SemanticBudget budget = {},
                     std::span<const CapturePreservation> preserved = {});
}  // namespace abcd::analysis
#endif
