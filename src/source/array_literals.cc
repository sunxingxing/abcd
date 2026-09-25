#include "array_literals.h"

namespace abcd::source {
namespace {
const analysis::ScalarAssignment* Scalar(const AstMethod& method, std::size_t i) {
  const auto* value = std::get_if<analysis::ScalarAssignment>(&method.instructions[i].operation);
  return value && value->completion == analysis::CompletionEffect::kNormal ? value : nullptr;
}
const analysis::ScalarValue* Value(const analysis::ScalarAssignment* assignment) {
  return assignment ? std::get_if<analysis::ScalarValue>(&assignment->expression) : nullptr;
}
std::optional<abc::RegisterId> Save(const AstMethod& method, std::size_t i) {
  const auto* assignment = Scalar(method, i);
  const auto* value = Value(assignment);
  const auto* target = assignment ? std::get_if<abc::RegisterId>(&assignment->destination) : nullptr;
  if (!target || !value || !std::holds_alternative<analysis::Accumulator>(*value)) return {};
  return *target;
}
bool Load(const AstMethod& method, std::size_t i, abc::RegisterId source) {
  const auto* assignment = Scalar(method, i);
  const auto* value = Value(assignment);
  const auto* reg = value ? std::get_if<abc::RegisterId>(value) : nullptr;
  return reg && *reg == source && std::holds_alternative<analysis::Accumulator>(assignment->destination);
}
}
std::optional<SourceFailure> FoldArrayLiterals(
    const AstMethod& method, SourceAst& ast, std::size_t bytes) {
  if (method.ssa.IsPhased()) return {};
  for (const auto& block : method.flow.blocks) {
    const auto end = std::size_t(block.first_instruction) + block.instruction_count;
    for (std::size_t begin = block.first_instruction; begin < end;) {
      const auto* initial = Scalar(method, begin);
      const auto* value = Value(initial);
      if (!value || !std::holds_alternative<analysis::EmptyArray>(*value) ||
          !std::holds_alternative<analysis::Accumulator>(initial->destination) ||
          begin + 1 >= end) { ++begin; continue; }
      const auto receiver = Save(method, begin + 1);
      if (!receiver) { ++begin; continue; }
      SourceArrayCommit commit{*receiver, {}};
      auto cursor = begin + 2;
      // Each element has a constant object producer, a spill/reload, and one
      // own-index definition. No user code, branch, alias publication or read
      // of the partially initialized array may enter this interval. Allocation
      // timing is unobservable under the existing no-OOM/debugger contract.
      constexpr std::size_t kElementInstructions = 4;
      while (end - cursor >= kElementInstructions) {
        if (!std::holds_alternative<analysis::BufferedObjectCreation>(
                method.instructions[cursor].operation)) break;
        const auto temporary = Save(method, cursor + 1);
        if (!temporary || *temporary == *receiver || !Load(method, cursor + 2, *temporary)) break;
        const auto* write = std::get_if<analysis::IndexedWrite>(
            &method.instructions[cursor + 3].operation);
        if (!write || write->object != *receiver || write->index != commit.elements.size()) break;
        // Grow geometrically but charge capacity before allocating; retained
        // plans share this budget rather than receiving one budget per array.
        if (commit.elements.size() == commit.elements.capacity()) {
          const auto old = commit.elements.capacity();
          const auto capacity = old ? old * 2 : 1;
          if (capacity < old || old > bytes / sizeof(analysis::InstructionId) ||
              capacity > bytes / sizeof(analysis::InstructionId) - old)
            return SourceFailure{SourceError::kResourceLimit, method.instructions[cursor].pc};
          commit.elements.reserve(capacity);
          if (commit.elements.capacity() > bytes / sizeof(analysis::InstructionId) - old)
            return SourceFailure{SourceError::kResourceLimit, method.instructions[cursor].pc};
        }
        commit.elements.push_back({static_cast<std::uint32_t>(cursor + 3)});
        cursor += kElementInstructions;
      }
      if (commit.elements.empty()) { ++begin; continue; }
      const auto retained = commit.elements.capacity() * sizeof(analysis::InstructionId);
      bytes -= retained;
      ast.allocated_bytes += retained;
      ast.array_captures += commit.elements.size();
      ast.statements[begin].operation = analysis::NoOperation{};
      ast.statements[begin + 1].operation = analysis::NoOperation{};
      for (const auto element : commit.elements)
        ast.statements[element.value].operation = SourceArrayCapture{element};
      auto& last = ast.statements[commit.elements.back().value];
      last.fused_read = ast.statements[begin].pc;
      last.read_distance = commit.elements.back().value - begin;
      last.operation = std::move(commit);
      begin = cursor;
    }
  }
  return {};
}
}  // namespace abcd::source
