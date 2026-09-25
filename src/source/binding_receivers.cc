#include "binding_receivers.h"

#include "private_copy.h"

namespace abcd::source {
namespace {
std::optional<ReceiverBinding> ReadBinding(
    const SourceExpression& expression,
    const analysis::SemanticOperation& operation) {
  if (const auto* module = std::get_if<ModuleExpression>(&expression)) {
    if (std::holds_alternative<analysis::ModuleLoad>(operation)) return *module;
  } else if (const auto* global = std::get_if<GlobalExpression>(&expression)) {
    const auto* read = std::get_if<analysis::GlobalAccess>(&operation);
    if (read && read->operation == analysis::GlobalOp::kReadName &&
        read->name == global->name) return *global;
  } else if (const auto* local = std::get_if<LocalExpression>(&expression)) {
    if (std::holds_alternative<analysis::LexicalLoad>(operation)) return *local;
  } else if (const auto* captured = std::get_if<CapturedExpression>(&expression)) {
    if (std::holds_alternative<analysis::LexicalLoad>(operation)) return *captured;
  }
  return {};
}
bool HasMergedOrigin(const SourceStatement& statement) {
  return statement.fused_read || statement.fused_spill || statement.fused_reload ||
         statement.previous_origin || statement.class_origin;
}
bool IsModuleGuard(const AstMethod& method, const SourceAst& ast,
                   std::size_t source, std::size_t position) {
  const auto& node = ast.statements[position];
  const auto* guard = std::get_if<analysis::HoleGuard>(&method.instructions[position].operation);
  return position == source + 1 && guard &&
      std::holds_alternative<analysis::Accumulator>(guard->value) &&
      std::holds_alternative<analysis::NoOperation>(node.operation) &&
      node.previous_origin == ast.statements[source].pc &&
      !node.fused_read && !node.fused_spill && !node.fused_reload && !node.class_origin;
}
bool HasReceiverUse(const AstMethod& method, const SourceAst& ast,
                    std::size_t source, std::size_t property,
                    std::span<const std::size_t> uses) {
  auto value = method.ssa.Write({static_cast<std::uint32_t>(source)});
  if (!value || value->value >= uses.size()) return false;
  auto remaining = uses[value->value];
  for (auto i = source + 1; i < property; ++i) {
    const auto id = analysis::InstructionId{static_cast<std::uint32_t>(i)};
    const auto reads = method.ssa.Reads(id);
    if (IsModuleGuard(method, ast, source, i)) {
      if (!remaining || reads.size() != 1 || reads.front().value != *value) return false;
      --remaining;
      continue;
    }
    const auto* save = std::get_if<analysis::ScalarAssignment>(&method.instructions[i].operation);
    const auto* target = save ? std::get_if<abc::RegisterId>(&save->destination) : nullptr;
    const auto* input = save ? std::get_if<analysis::ScalarValue>(&save->expression) : nullptr;
    if (!target || !input || !std::holds_alternative<analysis::Accumulator>(*input) ||
        i + 1 >= property) continue;
    const auto& next = ast.statements[i + 1];
    const auto* reload = std::get_if<analysis::ScalarAssignment>(&method.instructions[i + 1].operation);
    const auto* loaded = reload ? std::get_if<analysis::ScalarValue>(&reload->expression) : nullptr;
    const auto* reg = loaded ? std::get_if<abc::RegisterId>(loaded) : nullptr;
    const auto saved = method.ssa.Write(id);
    const auto next_id = analysis::InstructionId{static_cast<std::uint32_t>(i + 1)};
    const auto next_reads = method.ssa.Reads(next_id);
    if (!reload || !reg || *reg != *target ||
        !std::holds_alternative<analysis::Accumulator>(reload->destination) ||
        !std::holds_alternative<analysis::NoOperation>(next.operation) || HasMergedOrigin(next) ||
        !IsPureCopyStep(method.instructions[i + 1]) ||
        save->completion != analysis::CompletionEffect::kNormal ||
        reload->completion != analysis::CompletionEffect::kNormal ||
        remaining != 1 || reads.size() != 1 || reads.front().value != *value ||
        !saved || saved->value >= uses.size() || uses[saved->value] != 1 ||
        next_reads.size() != 1 || next_reads.front().value != *saved) return false;
    value = method.ssa.Write(next_id);
    if (!value || value->value >= uses.size()) return false;
    remaining = uses[value->value];
    ++i;
  }
  const auto reads = method.ssa.Reads({static_cast<std::uint32_t>(property)});
  return remaining == 1 && reads.size() == 1 && reads.front().value == *value;
}
}  // namespace

void FoldBindingReceivers(const AstMethod& method, SourceAst& ast,
                         std::span<const std::size_t> uses) {
  if (method.ssa.IsPhased() || ast.regions.catches || ast.argument_region) return;
  for (const auto& block : method.flow.blocks) {
    std::optional<std::size_t> previous;
    const auto end = std::size_t(block.first_instruction) + block.instruction_count;
    for (std::size_t i = block.first_instruction; i < end; ++i) {
      auto& tail = ast.statements[i];
      if (std::holds_alternative<analysis::NoOperation>(tail.operation) &&
          ((!HasMergedOrigin(tail) && IsPureCopyStep(method.instructions[i])) ||
           (previous && IsModuleGuard(method, ast, *previous, i)))) continue;
      const auto producer = previous;
      previous = i;
      auto* call = std::get_if<SourceStagedCall>(&tail.operation);
      if (!producer || !call || call->binding_receiver || HasMergedOrigin(tail)) continue;
      auto& head = ast.statements[*producer];
      const auto* assignment = std::get_if<SourceAssignment>(&head.operation);
      const auto binding = assignment
          ? ReadBinding(assignment->value, method.instructions[*producer].operation)
          : std::nullopt;
      if (!binding || HasMergedOrigin(head) ||
          assignment->result != AssignmentResult::kStored ||
          !std::holds_alternative<analysis::Accumulator>(assignment->target) ||
          !std::holds_alternative<analysis::NamedRead>(method.instructions[i].operation)) continue;
      const auto output = method.ssa.Write({static_cast<std::uint32_t>(*producer)});
      if (!output || !HasReceiverUse(method, ast, *producer, i, uses)) continue;
      const auto slot = method.ssa.Slot(*output);
      if (!slot || !std::holds_alternative<analysis::Accumulator>(*slot)) continue;
      // Read the binding once at the same effect boundary; no global lookup,
      // lexical capture/domain obligation or getter is bypassed. Arguments
      // still execute after the receiver's property Reference is obtained.
      call->binding_receiver = BindingReceiver{
          *binding, analysis::InstructionId{static_cast<std::uint32_t>(*producer)}};
      if (IsModuleGuard(method, ast, *producer, *producer + 1))
        call->binding_receiver->guard = analysis::InstructionId{
            static_cast<std::uint32_t>(*producer + 1)};
      head.operation = analysis::NoOperation{};
    }
  }
}
}  // namespace abcd::source
