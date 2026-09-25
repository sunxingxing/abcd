#include "argument_expression.h"

#include "argument_sequence.h"
#include "private_copy.h"

namespace abcd::source {
namespace {
bool ReachesArgument(const AstMethod& method, const SourceAst& ast,
                     std::size_t producer, std::size_t end, abc::RegisterId target,
                     analysis::SsaValueId definition, analysis::SsaValueId argument,
                     std::span<const std::size_t> uses) {
  // Follow only erased r -> acc -> r identity pairs backwards. Every value
  // must have one original remaining consumer, including phi uses. Distinct
  // ordered argument slots own disjoint chains; never scan a suffix per value.
  while (argument.value < uses.size() && uses[argument.value] == 1) {
    if (argument == definition) return true;
    const auto save_id = method.ssa.Definition(argument);
    if (!save_id || save_id->value <= producer + 1 || save_id->value >= end) return false;
    const auto load_id = analysis::InstructionId{save_id->value - 1};
    const auto& load_node = ast.statements[load_id.value];
    if (!std::holds_alternative<analysis::NoOperation>(load_node.operation) ||
        load_node.fused_read || load_node.fused_spill || load_node.fused_reload ||
        load_node.previous_origin || load_node.class_origin || load_node.literal_origin ||
        !IsRetargetedCopyAnchor(ast.statements[save_id->value], load_node)) return false;
    const auto& load_instruction = method.instructions[load_id.value];
    const auto& save_instruction = method.instructions[save_id->value];
    const auto* load = std::get_if<analysis::ScalarAssignment>(&load_instruction.operation);
    const auto* save = std::get_if<analysis::ScalarAssignment>(&save_instruction.operation);
    const auto* load_value = load
        ? std::get_if<analysis::ScalarValue>(&load->expression) : nullptr;
    const auto* save_value = save
        ? std::get_if<analysis::ScalarValue>(&save->expression) : nullptr;
    const auto* source = load_value ? std::get_if<abc::RegisterId>(load_value) : nullptr;
    const auto* destination = save ? std::get_if<abc::RegisterId>(&save->destination) : nullptr;
    if (!source || *source != target || !destination || *destination != target ||
        !save_value || !std::holds_alternative<analysis::Accumulator>(*save_value) ||
        !std::holds_alternative<analysis::Accumulator>(load->destination) ||
        load->completion != analysis::CompletionEffect::kNormal ||
        save->completion != analysis::CompletionEffect::kNormal ||
        !IsPureCopyStep(load_instruction) || !IsPureCopyStep(save_instruction)) return false;
    const auto loaded = method.ssa.Write(load_id);
    const auto saved = method.ssa.Write(*save_id);
    const auto load_reads = method.ssa.Reads(load_id);
    const auto save_reads = method.ssa.Reads(*save_id);
    if (!loaded || !saved || *saved != argument || loaded->value >= uses.size() ||
        uses[loaded->value] != 1 || load_reads.size() != 1 || save_reads.size() != 1 ||
        save_reads.front().value != *loaded) return false;
    argument = load_reads.front().value;
    // Enforce strictly descending instruction positions even for malformed SSA.
    end = load_id.value;
  }
  return false;
}
std::optional<analysis::SsaValueId> ArgumentDefinition(
    const AstMethod& method, const SourceAst& ast, std::size_t position,
    std::size_t end, abc::RegisterId target, std::span<const std::size_t> uses) {
  const auto& statement = ast.statements[position];
  if (statement.fused_read || statement.fused_spill || statement.fused_reload ||
      statement.class_origin || statement.literal_origin || statement.previous_origin) return {};
  const auto output = method.ssa.Write({static_cast<std::uint32_t>(position)});
  if (!output || output->value >= uses.size()) return {};
  const auto slot = method.ssa.Slot(*output);
  if (!slot) return {};
  if (const auto* reg = std::get_if<abc::RegisterId>(&*slot))
    return *reg == target ? output : std::nullopt;
  // Stored-expression and object-initialization folding retarget a producer
  // to its next private register save. The removed commit is that save's SSA definition,
  // not the earlier accumulator definition. Keep the origin anchor untouched.
  if (position + 1 >= end || uses[output->value] != 1 ||
      !IsRetargetedCopyAnchor(ast.statements[position + 1], statement)) return {};
  const auto& instruction = method.instructions[position + 1];
  const auto* save = std::get_if<analysis::ScalarAssignment>(&instruction.operation);
  const auto* saved = save ? std::get_if<abc::RegisterId>(&save->destination) : nullptr;
  const auto* input = save
      ? std::get_if<analysis::ScalarValue>(&save->expression) : nullptr;
  if (!saved || *saved != target || !input ||
      !std::holds_alternative<analysis::Accumulator>(*input) ||
      save->completion != analysis::CompletionEffect::kNormal ||
      !IsPureCopyStep(instruction)) return {};
  const auto id = analysis::InstructionId{static_cast<std::uint32_t>(position + 1)};
  const auto reads = method.ssa.Reads(id);
  if (reads.size() != 1 || reads.front().value != *output) return {};
  return method.ssa.Write(id);
}
}  // namespace

void FoldArgumentExpressions(const AstMethod& method, SourceAst& ast,
                             std::span<const std::size_t> uses) {
  if (ast.regions.catches || method.ssa.IsPhased()) return;
  for (const auto& statement : ast.statements) {
    const auto* staged = std::get_if<SourceStagedCall>(&statement.operation);
    if (!staged) continue;
    // Only this call owns the branch-argument IIFE. Other calls in the method
    // may still have ordered straight-line arguments. Match the printer's
    // native-argument admission; never mark an IIFE statement ArgumentOnly.
    if (ast.argument_region &&
        ast.argument_region->call == staged->call_statement) continue;
    const auto first = staged->first_argument_statement.value;
    const auto end = staged->call_statement.value;
    if (first > end || end >= ast.statements.size() ||
        end >= method.instructions.size() ||
        !HasOrderedArguments(std::span<const SourceStatement>{ast.statements}.subspan(
            first, end - first), staged->call)) continue;
    const auto* call = std::get_if<analysis::ReceiverCall>(&method.instructions[end].operation);
    if (!call || call->Count() != staged->call.Count()) continue;
    const auto reads = method.ssa.Reads(staged->call_statement);
    if (reads.size() < call->Count()) continue;
    // EffectOf(ReceiverCall) appends fixed/range arguments after callee and
    // receiver reads. Verify each suffix value's slot and exact definition;
    // no per-argument scan of all call reads or method uses is permitted.
    const auto arguments = reads.last(call->Count());
    std::uint32_t ordinal = 0;
    for (auto i = first; i < end; ++i) {
      auto* assignment = std::get_if<SourceAssignment>(&ast.statements[i].operation);
      if (!assignment) continue;
      const auto target = std::get<abc::RegisterId>(assignment->target);
      const auto& read = arguments[ordinal];
      const auto original = call->At(ordinal++);
      if (!original || *original != target) continue;
      const auto output = ArgumentDefinition(method, ast, i, end, target, uses);
      const auto slot = method.ssa.Slot(read.value);
      const auto* reg = slot ? std::get_if<abc::RegisterId>(&*slot) : nullptr;
      if (!output || !reg || *reg != target ||
          !ReachesArgument(method, ast, i, end, target, *output, read.value, uses)) continue;
      assignment->result = AssignmentResult::kArgumentOnly;
    }
  }
}
}  // namespace abcd::source
