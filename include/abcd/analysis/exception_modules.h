#ifndef ABCD_ANALYSIS_EXCEPTION_MODULES_H_
#define ABCD_ANALYSIS_EXCEPTION_MODULES_H_

#include "abcd/analysis/exception_value_flow.h"
#include "abcd/analysis/semantic_validation.h"

namespace abcd::analysis {
struct ExceptionModules {
  std::vector<ModuleReadFact> reads;
  std::vector<ExceptionLoadFact> loads;
  std::uint64_t work_units = 0;
  std::size_t AllocatedBytes() const {
    return reads.capacity() * sizeof(ModuleReadFact) +
           loads.capacity() * sizeof(ExceptionLoadFact);
  }
};
enum class ExceptionModuleError : std::uint8_t { kInvalidFlow, kResourceLimit, kInvalidPremise };
// Same immutable flow/SSA. Certifies adjacent module-load/static-name guard
// pairs only when both exceptional exits go to caller or the same dispatch
// target/Catch gateway, whose SSA definition overwrites accumulator. Guard
// has exactly one incoming phase edge from load Commit. No local catch sees
// removed Hole state; bypass entries and mismatched handlers remain unproven.
// Each load premise MUST have its corresponding guarded AST certificate.
// initialized_modules instead contains sorted unique original-coordinate LOCAL
// load sites whose owner proves initialization and Language-only later writes.
// These yield kInitialized source facts without moving a guard or throw edge.
// O(I+N+E+V) work and bounded O(I+N) storage for instructions, phase nodes,
// edges and SSA values. No per-load graph searches or write-table lookups.
[[nodiscard]] std::variant<ExceptionModules, ExceptionModuleError>
ProveExceptionModules(const ExceptionFlow& flow, const ExceptionSsa& ssa,
                      SemanticBudget budget = {},
                      std::span<const LanguageLoadFact> initialized_modules = {});
}  // namespace abcd::analysis
#endif
