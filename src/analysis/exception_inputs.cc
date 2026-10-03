#include "abcd/analysis/exception_inputs.h"

#include <bit>

#include "argument_inputs.h"

namespace abcd::analysis {
std::variant<ExceptionInputProof, ExceptionInputFailure> VerifyExceptionInputs(
    const ExceptionFlow& flow, const ExceptionSsa& ssa,
    const EnvironmentFlow& environments, const ExceptionValueFlow& values,
    std::uint64_t work, const ArgumentFrameFact* arguments, ExceptionInputDomain domain,
    std::span<const LanguageLoadFact> initialized_modules) {
  using Error = ExceptionInputError;
  ExceptionInputProof proof;
  const auto fail = [&](Error kind, abc::CodeOffset pc = {},
                       std::optional<ExceptionNodeId> node = {},
                       std::optional<SsaValueId> value = {}) {
    return ExceptionInputFailure{kind, pc, node, value, proof.work_units};
  };
  if (flow.nodes.size() != environments.before.size() ||
      flow.nodes.size() > UINT32_MAX ||
      values.Facts().size() != ssa.Values().size())
    return fail(Error::kMismatchedAnalysis);
  const auto reads = ssa.AllReads();
  std::optional<ArgumentWindow> window;
  std::size_t cursor = 0;
  const auto charge = [&](std::uint64_t units = 1) {
    if (units > work - proof.work_units) return false;
    proof.work_units += units;
    return true;
  };
  std::optional<InstructionId> previous_module;
  for (const auto& premise : initialized_modules) {
    if (!charge()) return fail(Error::kResourceLimit);
    if (premise.instruction.value >= flow.instructions.size() ||
        (previous_module && premise.instruction <= *previous_module)) return fail(Error::kMismatchedAnalysis);
    const auto* load = std::get_if<ModuleLoad>(&flow.instructions[premise.instruction.value].operation);
    if (!load || load->space != ModuleSpace::kLocal) return fail(Error::kMismatchedAnalysis);
    previous_module = premise.instruction;
  }
  proof.initialized_modules = initialized_modules;
  for (std::size_t i = 0; i < flow.nodes.size(); ++i) {
    if (!charge()) return fail(Error::kResourceLimit);
    const ExceptionNodeId id{static_cast<std::uint32_t>(i)};
    const auto& node = flow.nodes[i];
    if (node.instruction.value >= flow.instructions.size())
      return fail(Error::kMismatchedAnalysis, {}, id);
    const auto& instruction = flow.instructions[node.instruction.value];
    const auto pc = instruction.pc;
    const bool snapshot = node.phase == ExceptionPhase::kBefore &&
        environments.before[i] &&
        std::holds_alternative<ArgumentSnapshot>(instruction.operation);
    const bool forwarding = node.phase == ExceptionPhase::kBefore &&
        environments.before[i] && std::holds_alternative<SuperForward>(instruction.operation);
    if (snapshot || forwarding) {
      // The count/tail are implicit invocation inputs even when no declared
      // register is read. Never infer ownership from an empty SSA read list.
      if (!arguments || !flow.frame || arguments->frame != *flow.frame)
        return fail(Error::kArgumentOwner, pc, id);
      if (!window) {
        const auto built = ArgumentWindow::Build(arguments->frame);
        const auto* valid = std::get_if<ArgumentWindow>(&built);
        if (!valid) return fail(Error::kArgumentOwner, pc, id);
        window = *valid;
        proof.arguments = *arguments;
      }
    }
    if (snapshot) {
      if (!MatchesArgumentSnapshot(instruction, *window) ||
          node.successor_count > node.successors.size())
        return fail(Error::kMismatchedAnalysis, pc, id);
      std::optional<ExceptionNodeId> commit;
      for (const auto& edge : std::span(node.successors).first(node.successor_count)) {
        if (!charge()) return fail(Error::kResourceLimit, pc, id);
        if (edge.kind != ExceptionArcKind::kNormal) continue;
        if (commit || edge.target.value >= flow.nodes.size())
          return fail(Error::kMismatchedAnalysis, pc, id);
        commit = edge.target;
      }
      if (!commit || flow.nodes[commit->value].phase != ExceptionPhase::kCommit ||
          flow.nodes[commit->value].instruction != node.instruction)
        return fail(Error::kMismatchedAnalysis, pc, id);
      if (!charge(std::bit_width(ssa.Values().size()) + 1))
        return fail(Error::kResourceLimit, pc, id);
      const auto result = ssa.Write(*commit);
      if (!result || result->value >= ssa.Values().size())
        return fail(Error::kMismatchedAnalysis, pc, id);
      const auto& value = ssa.Values()[result->value];
      const auto* origin = std::get_if<InstructionValue>(&value.origin);
      if (!std::holds_alternative<Accumulator>(value.slot) || !origin ||
          origin->instruction.value != commit->value)
        return fail(Error::kMismatchedAnalysis, pc, id);
    }
    if (cursor < reads.size() && reads[cursor].instruction.value < i)
      return fail(Error::kMismatchedAnalysis, pc, id);
    std::uint32_t ordinal = 0;
    while (cursor < reads.size() && reads[cursor].instruction.value == i) {
      if (!charge()) return fail(Error::kResourceLimit, pc, id);
      const auto& read = reads[cursor++];
      if (node.phase != ExceptionPhase::kBefore || !environments.before[i])
        return fail(Error::kMismatchedAnalysis, pc, id, read.value);
      if (read.value.value >= values.Facts().size())
        return fail(Error::kMismatchedAnalysis, pc, id, read.value);
      if (snapshot) {
        const auto slot = window->At(ordinal);
        const auto* reg = std::get_if<abc::RegisterId>(&ssa.Values()[read.value.value].slot);
        if (!slot || read.ordinal != ordinal || !reg || *reg != *slot)
          return fail(Error::kMismatchedAnalysis, pc, id, read.value);
        ++ordinal;
      }
      const auto kinds = values.Facts()[read.value.value];
      const bool hole_transport = domain == ExceptionInputDomain::kHoleTransport &&
          !kinds.IsEmpty() && !kinds.Contains(VmValueKind::kEnvironment) &&
          !kinds.Contains(VmValueKind::kOtherInternal);
      if (!kinds.IsLanguage() && !hole_transport)
        return fail(Error::kInternalValue, pc, id, read.value);
      ++proof.reads;
    }
    if (snapshot && ordinal != window->Count())
      return fail(Error::kMismatchedAnalysis, pc, id);
  }
  if (cursor != reads.size()) return fail(Error::kMismatchedAnalysis);
  return proof;
}
}  // namespace abcd::analysis
