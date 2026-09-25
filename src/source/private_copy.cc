#include "private_copy.h"
#include "initialized_guard.h"

namespace abcd::source {
bool IsPureCopyStep(const analysis::SemanticInstruction& instruction) {
  return instruction.effects.normal == analysis::LanguageEffect::kNone &&
         instruction.effects.before_throw == analysis::LanguageEffect::kNone &&
         instruction.effects.exceptional ==
             analysis::ExceptionalCompletion::kImpossible;
}
bool IsRetargetedCopyAnchor(const SourceStatement& statement,
                           const SourceStatement& producer) {
  return std::holds_alternative<analysis::NoOperation>(statement.operation) &&
         statement.previous_origin == producer.pc && !statement.fused_read &&
         !statement.fused_spill && !statement.fused_reload &&
         !statement.class_origin && !statement.literal_origin;
}
std::optional<analysis::SsaValueId> RetargetedOutput(
    const AstMethod& method, const SourceAst& ast, std::size_t producer,
    std::size_t end, abc::RegisterId target, std::span<const std::size_t> uses) {
  if (producer >= end || end > ast.statements.size() || end > method.instructions.size()) return {};
  const auto output = method.ssa.Write({static_cast<std::uint32_t>(producer)});
  if (!output || output->value >= uses.size()) return {};
  const auto slot = method.ssa.Slot(*output);
  if (!slot || !std::holds_alternative<analysis::Accumulator>(*slot)) return {};
  auto next = producer + 1;
  std::size_t consumers = 1;  // The final save, plus each proved guard read.
  for (; next < end && IsElidedInitializedGuard(
             method, ast, next, ast.statements[next - 1].pc); ++next) {
    const auto reads = method.ssa.Reads({static_cast<std::uint32_t>(next)});
    if (reads.size() != 1 || reads.front().value != *output) return {};
    ++consumers;
  }
  if (next >= end || uses[output->value] != consumers ||
      !IsRetargetedCopyAnchor(ast.statements[next], ast.statements[next - 1])) return {};
  const auto& saved = method.instructions[next];
  const auto* save = std::get_if<analysis::ScalarAssignment>(&saved.operation);
  const auto* destination = save ? std::get_if<abc::RegisterId>(&save->destination) : nullptr;
  const auto* value = save ? std::get_if<analysis::ScalarValue>(&save->expression) : nullptr;
  if (!destination || *destination != target || !value ||
      !std::holds_alternative<analysis::Accumulator>(*value) || !IsPureCopyStep(saved) ||
      save->completion != analysis::CompletionEffect::kNormal) return {};
  const auto id = analysis::InstructionId{static_cast<std::uint32_t>(next)};
  const auto reads = method.ssa.Reads(id);
  if (reads.size() != 1 || reads.front().value != *output) return {};
  return method.ssa.Write(id);
}
std::optional<analysis::SsaValueId> PrivateCopyOutput(
    const AstMethod& method, const SourceAst& ast, std::size_t producer,
    std::size_t end, abc::RegisterId target, std::span<const std::size_t> uses) {
  const auto& source = method.instructions[producer];
  const auto* copy = std::get_if<analysis::ScalarAssignment>(&source.operation);
  if (!copy || !IsPureCopyStep(source) ||
      copy->completion != analysis::CompletionEffect::kNormal ||
      !std::holds_alternative<analysis::ScalarValue>(copy->expression)) return {};
  const auto output = method.ssa.Write({static_cast<std::uint32_t>(producer)});
  if (!output) return {};
  if (const auto* destination = std::get_if<abc::RegisterId>(&copy->destination))
    return *destination == target ? output : std::nullopt;
  return RetargetedOutput(method, ast, producer, end, target, uses);
}
}  // namespace abcd::source
