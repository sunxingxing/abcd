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
std::optional<SourceFailure> FoldArrayExpressions(
    const AstMethod& method, SourceAst& ast,
    std::span<const std::size_t> uses, std::size_t bytes) {
  for (const auto& block : method.flow.blocks) {
    const auto end = std::size_t(block.first_instruction) + block.instruction_count;
    for (std::size_t begin = block.first_instruction; begin + 2 < end;) {
      const auto* initial = Scalar(method, begin);
      const auto* value = Value(initial);
      if (!value || !std::holds_alternative<analysis::EmptyArray>(*value) ||
          !std::holds_alternative<analysis::Accumulator>(initial->destination)) {
        ++begin;
        continue;
      }
      const auto receiver = Save(method, begin + 1);
      const auto created = method.ssa.Write({static_cast<std::uint32_t>(begin)});
      const auto saved = method.ssa.Write({static_cast<std::uint32_t>(begin + 1)});
      if (!receiver || !created || !saved || uses[created->value] != 1) {
        ++begin;
        continue;
      }
      SourceArrayLiteral literal{*receiver,
          {static_cast<std::uint32_t>(begin + 1)}, {}};
      auto cursor = begin + 2;
      std::size_t matched_reads = 0;
      for (; cursor < end; ++cursor) {
        // The first post-construction read must immediately follow its last
        // definition. Original SSA counts rule out catch phis and escaped or
        // partially initialized array uses, even in a phased exception graph.
        if (Load(method, cursor, *receiver)) break;
        const auto& statement = ast.statements[cursor];
        const auto* write = std::get_if<SourceIndexedStore>(&statement.operation);
        if (write && (write->write.object != *receiver ||
                      write->write.index != literal.elements.size())) break;
        if (!write && !std::holds_alternative<SourceAssignment>(statement.operation) &&
            !std::holds_alternative<analysis::NoOperation>(statement.operation)) break;
        if (statement.fused_read || statement.fused_spill || statement.fused_reload ||
            statement.class_origin) break;
        // A nested array is a separate candidate, not an overlapping rescan.
        const auto* scalar = Scalar(method, cursor);
        const auto* nested = Value(scalar);
        if (nested && std::holds_alternative<analysis::EmptyArray>(*nested)) break;
        const auto& effects = method.instructions[cursor].effects;
        const auto* written = effects.normal_registers.write
            ? std::get_if<abc::RegisterId>(&*effects.normal_registers.write) : nullptr;
        if (written && *written == *receiver) break;
        std::size_t receiver_reads = 0;
        bool other_alias = false;
        for (const auto& read : method.ssa.Reads({static_cast<std::uint32_t>(cursor)})) {
          if (read.value == *saved) ++receiver_reads;
          const auto slot = method.ssa.Slot(read.value);
          const auto* reg = slot ? std::get_if<abc::RegisterId>(&*slot) : nullptr;
          if (reg && *reg == *receiver && read.value != *saved) other_alias = true;
        }
        if (other_alias || receiver_reads != (write ? 1U : 0U)) break;
        if (!write) continue;
        ++matched_reads;
        if (literal.elements.size() == literal.elements.capacity()) {
          const auto old = literal.elements.capacity();
          const auto capacity = old ? old * 2 : 1;
          if (capacity < old || old > bytes / sizeof(analysis::InstructionId) ||
              capacity > bytes / sizeof(analysis::InstructionId) - old)
            return SourceFailure{SourceError::kResourceLimit, statement.pc};
          literal.elements.reserve(capacity);
          if (literal.elements.capacity() > bytes / sizeof(analysis::InstructionId) - old)
            return SourceFailure{SourceError::kResourceLimit, statement.pc};
        }
        literal.elements.push_back({static_cast<std::uint32_t>(cursor)});
      }
      bool final_read = false;
      if (cursor < end && Load(method, cursor, *receiver)) {
        const auto reads = method.ssa.Reads({static_cast<std::uint32_t>(cursor)});
        final_read = reads.size() == 1 && reads.front().value == *saved;
      }
      if (final_read && !literal.elements.empty() &&
          literal.elements.back().value + 1 == cursor &&
          uses[saved->value] == matched_reads + 1) {
        const auto retained = literal.elements.capacity() * sizeof(analysis::InstructionId);
        bytes -= retained;
        ast.allocated_bytes += retained;
        ast.statements[begin].operation = std::move(literal);
      }
      // Candidate intervals are disjoint, including refusals. In particular,
      // nested creations resume here instead of repeatedly scanning a suffix.
      begin = cursor;
    }
  }
  return {};
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
          std::holds_alternative<SourceArrayLiteral>(ast.statements[begin].operation) ||
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
