#include "argument_inputs.h"

#include <bit>

namespace abcd::analysis {
bool MatchesArgumentSnapshot(const SemanticInstruction& instruction,
                             const ArgumentWindow& window) {
  const auto* snapshot = std::get_if<ArgumentSnapshot>(&instruction.operation);
  const auto& effect = instruction.effects;
  const auto& registers = effect.normal_registers;
  constexpr auto kEffects = LanguageEffect::kArgumentsRead | LanguageEffect::kAllocation;
  return snapshot && snapshot->window.First() == window.First() &&
      snapshot->window.Count() == window.Count() && registers.read_count == 0 &&
      registers.range && registers.range->first == window.First() &&
      registers.range->count == window.Count() && registers.write &&
      std::holds_alternative<Accumulator>(*registers.write) &&
      effect.normal == kEffects && effect.before_throw == kEffects &&
      effect.exceptional == ExceptionalCompletion::kMayThrow;
}

std::variant<ArgumentInputCheck, SemanticProofFailure> VerifyArgumentInputs(
    const abc::MethodBody& body, const SemanticMethod& method,
    std::uint64_t frame_size, std::uint32_t first_abi_register,
    const ArgumentFrameFact* arguments, SemanticBudget budget) {
  ArgumentInputCheck checked;
  const auto charge = [&](std::uint64_t count = 1) {
    if (count > budget.work - checked.work) return false;
    checked.work += count;
    return true;
  };
  using Error = SemanticProofError;
  if (body.instructions.size() != method.instructions.size())
    return SemanticProofFailure{Error::kSsaMismatch, {}};
  std::optional<ArgumentWindow> window;
  const auto reads = method.ssa.AllReads();
  const auto values = method.ssa.Values();
  std::size_t cursor = 0;
  for (std::size_t i = 0; i < body.instructions.size(); ++i) {
    const auto& site = body.instructions[i];
    const auto fail = [&](Error error) { return SemanticProofFailure{error, site.offset}; };
    if (!charge()) return fail(Error::kResourceLimit);
    const bool encoded = site.decoded.opcode == abc::Opcode::kGETUNMAPPEDARGS;
    const auto& instruction = method.instructions[i];
    const auto* snapshot = std::get_if<ArgumentSnapshot>(&instruction.operation);
    if (encoded != (snapshot != nullptr)) return fail(Error::kSsaMismatch);
    if (!snapshot) continue;
    if (!arguments) return fail(Error::kArgumentOwner);
    if (!window) {
      if (arguments->frame.registers != frame_size ||
          arguments->frame.locals != first_abi_register)
        return fail(Error::kArgumentOwner);
      const auto built = ArgumentWindow::Build(arguments->frame);
      const auto* valid = std::get_if<ArgumentWindow>(&built);
      if (!valid) return fail(Error::kArgumentOwner);
      window = *valid;
      checked.fact = *arguments;
    }
    if (site.decoded.operand_count != 0 || instruction.pc != site.offset ||
        !MatchesArgumentSnapshot(instruction, *window))
      return fail(Error::kSsaMismatch);
    const InstructionId id{static_cast<std::uint32_t>(i)};
    while (cursor < reads.size() && reads[cursor].instruction < id) {
      if (!charge()) return fail(Error::kResourceLimit);
      ++cursor;
    }
    std::uint32_t ordinal = 0;
    while (cursor < reads.size() && reads[cursor].instruction == id) {
      if (!charge()) return fail(Error::kResourceLimit);
      const auto& read = reads[cursor++];
      const auto slot = window->At(ordinal);
      if (!slot || read.ordinal != ordinal || read.value.value >= values.size())
        return fail(Error::kSsaMismatch);
      const auto* reg = std::get_if<abc::RegisterId>(&values[read.value.value].slot);
      if (!reg || *reg != *slot) return fail(Error::kSsaMismatch);
      ++ordinal;
    }
    if (ordinal != window->Count()) return fail(Error::kSsaMismatch);
    if (!charge(std::bit_width(values.size()) + 1)) return fail(Error::kResourceLimit);
    const auto result = method.ssa.Write(id);
    if (!result || result->value >= values.size()) return fail(Error::kSsaMismatch);
    const auto& value = values[result->value];
    const auto* origin = std::get_if<InstructionValue>(&value.origin);
    if (!std::holds_alternative<Accumulator>(value.slot) || !origin || origin->instruction != id)
      return fail(Error::kSsaMismatch);
  }
  return checked;
}
}  // namespace abcd::analysis
