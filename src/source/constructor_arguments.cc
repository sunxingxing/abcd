#include "constructor_arguments.h"

#include "argument_sequence.h"

namespace abcd::source {
namespace {
bool Preparation(const SourceStatement& node) {
  if (std::holds_alternative<analysis::NoOperation>(node.operation)) return true;
  const auto* assignment = std::get_if<SourceAssignment>(&node.operation);
  return assignment && assignment->result == AssignmentResult::kStored &&
         HasArgumentValue(assignment->value) &&
         !std::holds_alternative<analysis::ConstructorCall>(assignment->value);
}
}  // namespace

void RecoverConstructorArguments(const AstMethod& method, SourceAst& ast) {
  if (method.ssa.IsPhased() || ast.regions.catches || ast.argument_region ||
      ast.super_arguments || method.instructions.size() != ast.statements.size()) return;
  for (const auto& block : method.flow.blocks) {
    const auto end = std::size_t(block.first_instruction) + block.instruction_count;
    if (end > ast.statements.size()) return;
    auto first = std::size_t(block.first_instruction);
    for (auto i = first; i < end; ++i) {
      auto& node = ast.statements[i];
      if (const auto* literal = std::get_if<SourceArrayLiteral>(&node.operation)) {
        if (literal->elements.empty() || literal->elements.back().value >= end) return;
        i = literal->elements.back().value;
        first = i + 1;
        continue;
      }
      if (const auto* staged = std::get_if<SourceStagedCall>(&node.operation)) {
        // Never steal nodes already owned by a member-call argument interval.
        if (staged->call_statement.value >= end) return;
        i = staged->call_statement.value;
        first = i + 1;
        continue;
      }
      const auto* assignment = std::get_if<SourceAssignment>(&node.operation);
      const auto* call = assignment
          ? std::get_if<analysis::ConstructorCall>(&assignment->value) : nullptr;
      if (!call) {
        if (!Preparation(node)) first = i + 1;
        continue;
      }
      const auto begin = first;
      first = i + 1;
      const auto* original = std::get_if<analysis::ConstructorCall>(
          &method.instructions[i].operation);
      if (!original || !call->ArgumentCount() ||
          call->inputs.first != original->inputs.first ||
          call->inputs.count != original->inputs.count ||
          node.fused_read || node.fused_spill || node.fused_reload ||
          node.previous_origin || node.class_origin || node.literal_origin ||
          assignment->result == AssignmentResult::kArgumentOnly) continue;
      const auto reads = method.ssa.Reads({static_cast<std::uint32_t>(i)});
      if (reads.size() != call->inputs.count) continue;
      bool inputs = true;
      for (std::size_t ordinal = 0; ordinal < reads.size(); ++ordinal) {
        const auto slot = method.ssa.Slot(reads[ordinal].value);
        const auto* reg = slot ? std::get_if<abc::RegisterId>(&*slot) : nullptr;
        if (!reg || reg->value != call->inputs.first.value + ordinal) {
          inputs = false;
          break;
        }
      }
      if (!inputs) continue;
      // Candidates between consecutive constructors are disjoint. Scan the
      // suffix once: only acc and argument-window commits may enter it. The
      // constructor's private binding is therefore stable when read earlier.
      auto start = i;
      bool argument_write = false;
      while (start > begin) {
        const auto& prior = ast.statements[start - 1];
        const auto* value = std::get_if<SourceAssignment>(&prior.operation);
        if (value) {
          if (const auto* reg = std::get_if<abc::RegisterId>(&value->target)) {
            if (reg->value <= call->inputs.first.value ||
                reg->value - call->inputs.first.value >= call->inputs.count) break;
            argument_write = true;
          }
        }
        --start;
      }
      if (!argument_write) continue;
      const ConstructorArguments region{{static_cast<std::uint32_t>(start)},
                                         {static_cast<std::uint32_t>(i)}};
      for (auto member = start; member <= i; ++member)
        ast.statements[member].constructor_arguments = region;
    }
  }
}
}  // namespace abcd::source
