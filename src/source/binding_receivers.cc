#include "binding_receivers.h"

#include "private_copy.h"
#include "initialized_guard.h"

namespace abcd::source {
namespace {
std::optional<ReceiverBinding> ReadBinding(
    const SourceExpression& expression,
    const analysis::SemanticInstruction& instruction) {
  const auto& operation = instruction.operation;
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
  } else if (const auto* member = std::get_if<MemberExpression>(&expression)) {
    const auto* read = std::get_if<analysis::NamedRead>(&operation);
    // Preserve one getter evaluation as the next receiver. Already-composed
    // receivers have their own origin coordinates and must not be flattened
    // until the whole expression path can carry every source/guard origin.
    if (read && read->name == member->name && !member->receiver)
      return PropertyReceiver{member->object, member->name};
  } else if (const auto* scalar =
                 std::get_if<analysis::ScalarExpression>(&expression)) {
    const auto* value = std::get_if<analysis::ScalarValue>(scalar);
    const auto* reg = value ? std::get_if<abc::RegisterId>(value) : nullptr;
    const auto* copy = std::get_if<analysis::ScalarAssignment>(&operation);
    const auto* input = copy
        ? std::get_if<analysis::ScalarValue>(&copy->expression) : nullptr;
    const auto* original = input ? std::get_if<abc::RegisterId>(input) : nullptr;
    if (reg && original && *reg == *original &&
        std::holds_alternative<analysis::Accumulator>(copy->destination) &&
        copy->completion == analysis::CompletionEffect::kNormal &&
        IsPureCopyStep(instruction)) return *reg;
  }
  return {};
}
bool HasMergedOrigin(const SourceStatement& statement) {
  return statement.fused_read || statement.fused_spill || statement.fused_reload ||
         statement.previous_origin || statement.class_origin || statement.literal_origin;
}
bool IsReceiverGuard(const AstMethod& method, const SourceAst& ast,
                   std::size_t source, std::size_t position) {
  if (position != source + 1) return false;
  const auto& node = ast.statements[position];
  const auto& instruction = method.instructions[position];
  if (!std::holds_alternative<analysis::NoOperation>(node.operation) ||
      node.fused_read || node.fused_spill || node.fused_reload || node.class_origin ||
      node.literal_origin || instruction.effects.normal_registers.write) return false;
  if (const auto* guard = std::get_if<analysis::HoleGuard>(&instruction.operation))
    return std::holds_alternative<analysis::Accumulator>(guard->value) &&
           node.previous_origin == ast.statements[source].pc;
  return IsElidedInitializedGuard(method, ast, position);
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
    if (IsReceiverGuard(method, ast, source, i)) {
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
  if (method.ssa.IsPhased() || ast.regions.catches) return;
  // Branching arguments execute after the property Reference is obtained.
  // Only the receiver-to-property interval must stay in one basic block;
  // never extend this proof across the argument region's joins or effects.
  for (const auto& block : method.flow.blocks) {
    std::optional<std::size_t> previous;
    const auto end = std::size_t(block.first_instruction) + block.instruction_count;
    for (std::size_t i = block.first_instruction; i < end; ++i) {
      auto& tail = ast.statements[i];
      if (std::holds_alternative<analysis::NoOperation>(tail.operation) &&
          ((!HasMergedOrigin(tail) && IsPureCopyStep(method.instructions[i])) ||
           (previous && IsReceiverGuard(method, ast, *previous, i)))) continue;
      const auto producer = previous;
      previous = i;
      auto* call = std::get_if<SourceStagedCall>(&tail.operation);
      auto* property_assignment = std::get_if<SourceAssignment>(&tail.operation);
      auto* member = property_assignment
          ? std::get_if<MemberExpression>(&property_assignment->value) : nullptr;
      if (!producer || (!call && !member) || HasMergedOrigin(tail) ||
          (call && call->binding_receiver) ||
          (member && (member->receiver || property_assignment->object_field ||
                      !std::holds_alternative<analysis::Accumulator>(member->object)))) continue;
      auto& head = ast.statements[*producer];
      const auto* assignment = std::get_if<SourceAssignment>(&head.operation);
      const auto binding = assignment
          ? ReadBinding(assignment->value, method.instructions[*producer])
          : std::nullopt;
      const auto* original = std::get_if<analysis::NamedRead>(&method.instructions[i].operation);
      if (!binding || HasMergedOrigin(head) ||
          assignment->result != AssignmentResult::kStored ||
          !std::holds_alternative<analysis::Accumulator>(assignment->target) ||
          !original || original->name != (call ? call->name : member->name)) continue;
      const auto output = method.ssa.Write({static_cast<std::uint32_t>(*producer)});
      if (!output || !HasReceiverUse(method, ast, *producer, i, uses)) continue;
      const auto slot = method.ssa.Slot(*output);
      if (!slot || !std::holds_alternative<analysis::Accumulator>(*slot)) continue;
      // Read the binding/property once at the same effect boundary; no global lookup,
      // lexical capture/domain obligation or getter is bypassed. Arguments
      // still execute after the receiver's property Reference is obtained.
      auto receiver = BindingReceiver{
          *binding, analysis::InstructionId{static_cast<std::uint32_t>(*producer)}};
      if (IsReceiverGuard(method, ast, *producer, *producer + 1))
        receiver.guard = analysis::InstructionId{
            static_cast<std::uint32_t>(*producer + 1)};
      if (call) call->binding_receiver = receiver;
      else member->receiver = MemberReceiver{
          receiver, analysis::InstructionId{static_cast<std::uint32_t>(i)}};
      head.operation = analysis::NoOperation{};
    }
  }
}
}  // namespace abcd::source
