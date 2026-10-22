#include "abcd/analysis/exception_edge.h"

namespace abcd::analysis {
std::optional<ExceptionEdge> ResolveExceptionEdge(
    const SemanticInstruction& instruction, const ExceptionDispatch& dispatch) {
  switch (instruction.effects.exceptional) {
    case ExceptionalCompletion::kImpossible:
      return {};
    case ExceptionalCompletion::kMayThrow:
    case ExceptionalCompletion::kAlwaysThrow:
      break;
  }
  ExceptionValue value = PendingException{};
  if (const auto* thrown = std::get_if<ThrowOperation>(&instruction.operation);thrown&&thrown->kind==ThrowKind::kValue)
    value = ThrownValue{thrown->value};
  return ExceptionEdge{instruction.pc, dispatch.Find(instruction.pc), value,
                       instruction.effects.exceptional};
}
}  // namespace abcd::analysis
