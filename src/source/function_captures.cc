#include "function_captures.h"

#include <algorithm>

namespace abcd::source {
std::variant<std::uint64_t, FunctionCaptureError> VerifyFunctionCaptures(
    const FunctionInput& input, const analysis::ValueProgram& graph,
    const analysis::ValueEnvironments& environments, analysis::SemanticBudget budget) {
  using Error = FunctionCaptureError;
  const auto verified = analysis::VerifyValueEnvironments(graph, environments, budget);
  if (const auto* error = std::get_if<analysis::ValueEnvironmentError>(&verified)) {
    if (*error == analysis::ValueEnvironmentError::kResourceLimit) return Error::kResourceLimit;
    if (*error == analysis::ValueEnvironmentError::kWorkLimit) return Error::kWorkLimit;
    return Error::kEnvironment;
  }
  auto remaining = budget.work - std::get<std::uint64_t>(verified);
  const auto step = [&] {
    if (remaining == 0) throw Error::kWorkLimit;
    --remaining;
  };
  try {
    const auto accesses = input.CaptureAccesses();
    const auto captures = input.Captures();
    if (captures.size() != accesses.size()) return Error::kCoverage;
    std::size_t count = 0;
    for (const auto& binding : environments.flow.bindings) {
      step();
      const auto* depth = std::get_if<analysis::CapturedDepth>(&binding.binding.environment);
      if (!depth) continue;
      ++count;
      // The fresh environment checker established the operation bounds and
      // unique lexical-site ordering. Each original site maps back to this op,
      // so equal total counts cannot hide duplicated or dropped occurrences.
      const auto& op = graph.ops[binding.instruction];
      if (op.origin.value >= graph.origins.size()) return Error::kOrigin;
      const auto& origin = graph.origins[op.origin.value];
      if (origin.disposition != analysis::OriginDisposition::kRetained ||
          origin.site.value >= graph.input.sites.size() ||
          graph.input.sites[origin.site.value] != analysis::OpId{binding.instruction} ||
          origin.site.value >= input.Input().instructions.size()) return Error::kOrigin;
      const auto& original = input.Input().instructions[origin.site.value];
      if (origin.pc != original.pc || origin.original != original.origin) return Error::kOrigin;
      const auto found = std::lower_bound(accesses.begin(), accesses.end(), origin.site.value,
          [&](const auto& access, std::uint32_t site) { step(); return access.instruction.value < site; });
      if (found == accesses.end() || found->instruction.value != origin.site.value) return Error::kCoverage;
      const auto& expected = captures[static_cast<std::size_t>(found - accesses.begin())].cell;
      if (*depth != expected.depth || binding.binding.slot.value != expected.slot.value)
        return Error::kBinding;
      switch (found->kind) {
        case analysis::LocalAccessKind::kLoad:
          if (!std::holds_alternative<analysis::ValueLexicalLoad>(op.operation)) return Error::kBinding;
          break;
        case analysis::LocalAccessKind::kStore:
          if (!std::holds_alternative<analysis::ValueLexicalStore>(op.operation)) return Error::kBinding;
          break;
      }
    }
    if (count != captures.size()) return Error::kCoverage;
    return budget.work - remaining;
  } catch (Error error) { return error; }
}
}  // namespace abcd::source
