#ifndef ABCD_ANALYSIS_VALUE_ENVIRONMENT_H_
#define ABCD_ANALYSIS_VALUE_ENVIRONMENT_H_

#include "abcd/analysis/environment_flow.h"
#include "abcd/analysis/value_ir.h"

namespace abcd::analysis {
struct ValueEnvironments {
  IrRevision revision;
  CfgRevision cfg_revision;
  // before/bindings/allocations use OpId positions, never decoded instruction
  // positions after a rewrite. Index IDs are local to THIS result. Allocations
  // still denote sites with potentially many dynamic instances.
  EnvironmentFlow flow;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
  // Present only for phased graphs: entry state of every work block, including
  // empty Commit/Catch blocks. Operation views above retain OpId coordinates.
  std::vector<std::optional<EnvironmentRef>> phase_entries;
  // Successful completion state, OpId indexed. Before/exception paths keep
  // flow.before; allocation/pop never change that state prematurely. Absence
  // denotes unreachable execution or an operation with no normal completion.
  std::vector<std::optional<EnvironmentRef>> normal_after;
};
enum class ValueEnvironmentError : std::uint8_t {
  kRevision, kFlow, kOrigin, kState, kAllocation, kBinding, kResourceLimit, kWorkLimit
};
// Unchanged normal work graph with input/effect correspondence already proved.
// Computes cell/environment topology, not initialization, mutability, source
// scopes or capture permission. Removed operations must be proved pure elisions.
[[nodiscard]] std::variant<ValueEnvironments, ValueEnvironmentError> BuildValueEnvironments(
    const ValueProgram& graph, SemanticBudget budget = {});
// Checks the supplied state transitions, edge joins, allocation sites and exact
// lexical bindings against fresh flow. Does not rerun the environment planner.
[[nodiscard]] std::variant<std::uint64_t, ValueEnvironmentError> VerifyValueEnvironments(
    const ValueProgram& graph, const ValueEnvironments& environments, SemanticBudget budget = {});
// Phased counterpart: successful Commit changes the environment chain; Before
// binds lexical accesses and propagates the old chain on exceptional exits.
// Requires independently proved input/effect correspondence. Like the normal
// planner, this proves topology only, never initialization or capture permission.
[[nodiscard]] std::variant<ValueEnvironments, ValueEnvironmentError> BuildPhasedValueEnvironments(
    const ValueProgram& graph, SemanticBudget budget = {});
[[nodiscard]] std::variant<std::uint64_t, ValueEnvironmentError> VerifyPhasedValueEnvironments(
    const ValueProgram& graph, const ValueEnvironments& environments, SemanticBudget budget = {});
}  // namespace abcd::analysis
#endif
