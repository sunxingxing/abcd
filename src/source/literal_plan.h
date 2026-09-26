#ifndef ABCD_SOURCE_LITERAL_PLAN_H_
#define ABCD_SOURCE_LITERAL_PLAN_H_

#include "abcd/analysis/value_ir.h"

namespace abcd::source {
struct ObjectInit {
  analysis::OpId allocation;
  std::uint32_t first;
  std::uint32_t count;
};
struct LiteralPlan {
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  std::vector<ObjectInit> objects;
  std::vector<analysis::OpId> definitions;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
enum class LiteralError : std::uint8_t {
  kRevision, kFlow, kDefinition, kReceiver, kObserved, kLateValue, kDomain,
  kCoverage, kResourceLimit, kWorkLimit
};
// Initial domain: fresh empty ordinary object, same-block named own definitions,
// no observation until initialization completes, RHS available before allocation.
// Not AST/publication permission: preserve snapshots, computed keys and sites.
[[nodiscard]] std::variant<LiteralPlan, LiteralError> BuildLiteralPlan(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget = {});
// Checks supplied groups against use chains and fresh flow, without regrouping.
[[nodiscard]] std::variant<std::uint64_t, LiteralError> VerifyLiteralPlan(
    const analysis::ValueProgram& graph, const LiteralPlan& plan,
    analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
