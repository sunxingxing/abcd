#ifndef ABCD_SOURCE_EXPRESSION_FOREST_H_
#define ABCD_SOURCE_EXPRESSION_FOREST_H_

#include "binding_plan.h"
#include "logical_regions.h"
#include "call_plan.h"
#include "expression_constants.h"

namespace abcd::source {
struct EdgeExpressions {
  analysis::IncomingKey incoming;
  std::uint32_t first;
  std::uint32_t count;
};
struct ThrowExpressions {
  ThrowCopies placement;
  std::uint32_t first;
  std::uint32_t count;
};
struct CatchExpressions {
  analysis::WorkBlockId block;
  std::uint32_t first;
  std::uint32_t count;
};
struct ExpressionForest {
  BindingPlan bindings;
  CallPlan calls;
  std::vector<ExprNode> expressions;
  std::vector<DataProperty> properties;
  std::vector<std::optional<PropertyId>> initializers;
  std::vector<ExprId> arguments;
  std::vector<SymbolId> closure_captures;
  std::vector<TreeOrigin> origins;
  std::vector<TreeOriginId> origin_parts;
  // A branch maps to its condition; a return maps to its returned expression.
  // Jumps, environment placements and removed operations have no expression. Statement assembly owns
  // control-operation placements, not this expression-only construction step.
  std::vector<std::optional<ExprId>> operations;
  std::vector<std::optional<SymbolId>> module_initializations;
  // Flat, exclusively owned assignment roots, in each verified copy schedule's
  // order. These are not operation expressions: statement assembly places them
  // on the exact edge (including empty arms), or once at MethodEntry before the
  // original body, after the language's formal-parameter initialization.
  std::vector<ExprId> edge_assignments;
  std::vector<EdgeExpressions> edges;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
  // Disjoint root ranges: stage before a throwing operation, commit only at
  // Catch entry. Neither is an ordinary edge assignment or operation subtree.
  std::vector<ExprId> throw_assignments;
  std::vector<ThrowExpressions> throws;
  std::vector<ExprId> catch_assignments;
  std::vector<CatchExpressions> catches;
  // First omitted operation at a closed expression boundary. The unchanged
  // graph remains authoritative; no fake terminator or residual CFG is made.
  std::optional<analysis::OpId> recovery_stop;
};
enum class ExpressionError : std::uint8_t {
  kBinding, kOrigin, kConstant, kUnavailableValue, kUnsupportedOperation,
  kOwnership, kResourceLimit, kWorkLimit
};
// Inputs must have passed import/rewrite verification. Builds each inline tree
// once, in operation order, without recursion or sharing effectful expressions.
// Output is NOT a frozen/publication-ready tree; scope/control/source proofs
// still have to be checked after statement assembly.
// Module symbols are the caller's conditional alias order. Their local SymbolIds
// append after bindings.symbols; FunctionTree must retain that exact order.
// Optional names enable buffered overwrite folding by actual key spelling;
// without them preserve the original buffer plus ordered definition suffix.
// Logical selections come from FindLogicalExpressions for the same graph and
// plans; final TreeFlow independently checks their actual syntax/edge ownership.
// AllowPartial can return recovery_stop for a supported straight-line prefix;
// its expressions are still exclusively owned by the retained root operations.
[[nodiscard]] std::variant<ExpressionForest, ExpressionError>
BuildExpressionForest(const analysis::ValueProgram& graph,
    const EvaluationPlan& evaluation, std::uint32_t parameter_count,
    FunctionOwnerId owner, ExpressionConstants constants,
    analysis::SemanticBudget budget = {}, std::span<const ModuleSymbol> modules = {},
    RecoveryMode recovery = RecoveryMode::kCompleteOnly,
    std::span<const std::u16string> names = {}, std::span<const LogicalRegion> logicals = {});
[[nodiscard]] std::variant<ExpressionForest, ExpressionError>
BuildPhasedExpressionForest(const analysis::ValueProgram& graph,
    const EvaluationPlan& evaluation, std::uint32_t parameter_count,
    FunctionOwnerId owner, ExpressionConstants constants,
    analysis::SemanticBudget budget = {}, std::span<const ModuleSymbol> modules = {},
    RecoveryMode recovery = RecoveryMode::kCompleteOnly,
    std::span<const std::u16string> names = {}, std::span<const LogicalRegion> logicals = {});
}  // namespace abcd::source
#endif
