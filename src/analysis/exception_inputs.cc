#include "abcd/analysis/exception_inputs.h"

namespace abcd::analysis {
std::variant<ExceptionInputProof, ExceptionInputFailure> VerifyExceptionInputs(
    const ExceptionFlow& flow, const ExceptionSsa& ssa,
    const EnvironmentFlow& environments, const ExceptionValueFlow& values,
    std::uint64_t work) {
  using Error = ExceptionInputError;
  const auto fail = [](Error kind, abc::CodeOffset pc = {},
                       std::optional<ExceptionNodeId> node = {},
                       std::optional<SsaValueId> value = {}) {
    return ExceptionInputFailure{kind, pc, node, value};
  };
  if (flow.nodes.size() != environments.before.size() ||
      flow.nodes.size() > UINT32_MAX ||
      values.Facts().size() != ssa.Values().size())
    return fail(Error::kMismatchedAnalysis);
  ExceptionInputProof proof;
  const auto reads = ssa.AllReads();
  std::size_t cursor = 0;
  const auto charge = [&] {
    if (proof.work_units == work) return false;
    ++proof.work_units;
    return true;
  };
  for (std::size_t i = 0; i < flow.nodes.size(); ++i) {
    if (!charge()) return fail(Error::kResourceLimit);
    const ExceptionNodeId id{static_cast<std::uint32_t>(i)};
    const auto& node = flow.nodes[i];
    if (node.instruction.value >= flow.instructions.size())
      return fail(Error::kMismatchedAnalysis, {}, id);
    const auto pc = flow.instructions[node.instruction.value].pc;
    if (cursor < reads.size() && reads[cursor].instruction.value < i)
      return fail(Error::kMismatchedAnalysis, pc, id);
    while (cursor < reads.size() && reads[cursor].instruction.value == i) {
      if (!charge()) return fail(Error::kResourceLimit, pc, id);
      const auto& read = reads[cursor++];
      if (node.phase != ExceptionPhase::kBefore || !environments.before[i])
        return fail(Error::kMismatchedAnalysis, pc, id, read.value);
      if (read.value.value >= values.Facts().size())
        return fail(Error::kMismatchedAnalysis, pc, id, read.value);
      if (!values.Facts()[read.value.value].IsLanguage())
        return fail(Error::kInternalValue, pc, id, read.value);
      ++proof.reads;
    }
  }
  if (cursor != reads.size()) return fail(Error::kMismatchedAnalysis);
  return proof;
}
}  // namespace abcd::analysis
