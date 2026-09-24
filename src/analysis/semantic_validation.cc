#include "abcd/analysis/semantic_validation.h"

#include <new>
#include <stdexcept>

namespace abcd::analysis {
namespace {
SemanticProofFailure Fail(SemanticProofError kind,
                          abc::CodeOffset pc = {}) {
  return {kind, pc};
}
}  // namespace

SemanticProofResult VerifySemanticSource(const abc::MethodBody& body,
                                         const SemanticMethod& method,
                                         std::uint64_t frame_size,
                                         std::uint32_t first_abi_register,
                                         SemanticBudget budget) {
  if (first_abi_register > frame_size ||
      method.instructions.size() != body.instructions.size() ||
      method.allocated_bytes > budget.bytes)
    return Fail(SemanticProofError::kResourceLimit);
  for (std::size_t i = 0; i < body.instructions.size(); ++i) {
    if (method.instructions[i].pc != body.instructions[i].offset ||
        method.ssa.Write(InstructionId{static_cast<std::uint32_t>(i)})
            .has_value() !=
            method.instructions[i].effects.normal_registers.write.has_value())
      return Fail(SemanticProofError::kSsaMismatch,
                  body.instructions[i].offset);
  }
  auto remaining = budget.bytes - method.allocated_bytes;
  auto accumulator = AnalyzeAcyclicAccumulator(
      body, method.flow, frame_size, {remaining});
  auto* accumulated = std::get_if<AccumulatorFlow>(&accumulator);
  if (!accumulated) {
    const auto& failure = std::get<AccumulatorFailure>(accumulator);
    return Fail(failure.kind == AccumulatorError::kResourceLimit
                    ? SemanticProofError::kResourceLimit
                    : SemanticProofError::kAccumulator,
                failure.offset);
  }
  if (accumulated->allocated_bytes > remaining)
    return Fail(SemanticProofError::kResourceLimit);
  remaining -= accumulated->allocated_bytes;
  auto environments = AnalyzeEnvironments(
      body, method.flow, frame_size, {remaining});
  auto* bound = std::get_if<EnvironmentFlow>(&environments);
  if (!bound) {
    const auto& failure = std::get<EnvironmentFlowFailure>(environments);
    return Fail(failure.kind == EnvironmentFlowError::kResourceLimit
                    ? SemanticProofError::kResourceLimit
                    : SemanticProofError::kEnvironment,
                failure.offset);
  }
  if (bound->allocated_bytes > remaining)
    return Fail(SemanticProofError::kResourceLimit);
  remaining -= bound->allocated_bytes;
  if (method.ssa.Values().size() > remaining / sizeof(EntryValueFact))
    return Fail(SemanticProofError::kResourceLimit);
  try {
    std::vector<EntryValueFact> entries;
    entries.reserve(method.ssa.Values().size());
    if (entries.capacity() > remaining / sizeof(EntryValueFact))
      return Fail(SemanticProofError::kResourceLimit);
    for (std::size_t i = 0; i < method.ssa.Values().size(); ++i) {
      const auto& value = method.ssa.Values()[i];
      if (std::holds_alternative<MethodEntry>(value.origin) &&
          std::holds_alternative<abc::RegisterId>(value.slot) &&
          std::get<abc::RegisterId>(value.slot).value >= first_abi_register)
        entries.push_back({SsaValueId{static_cast<std::uint32_t>(i)},
                           ValueKinds::Language()});
    }
    remaining -= entries.capacity() * sizeof(EntryValueFact);
    IdfQuota quota{budget.work};
    auto values = RegisterValueFlow::Build(
        body, method.ssa, frame_size, entries, quota, {remaining});
    auto* analyzed = std::get_if<RegisterValueFlow>(&values);
    if (!analyzed) {
      const auto& failure = std::get<ValueFlowFailure>(values);
      const auto pc = failure.instruction &&
                              failure.instruction->value < body.instructions.size()
                          ? body.instructions[failure.instruction->value].offset
                          : abc::CodeOffset{};
      return Fail(failure.kind == ValueFlowError::kResourceLimit
                      ? SemanticProofError::kResourceLimit
                      : SemanticProofError::kRegisterValue,
                  pc);
    }
    for (std::size_t i = 0; i < body.instructions.size(); ++i) {
      for (const auto& read : method.ssa.Reads(
               InstructionId{static_cast<std::uint32_t>(i)})) {
        const auto kinds = analyzed->At(read.value);
        if (!kinds || !kinds->IsLanguage())
          return Fail(SemanticProofError::kInternalValue,
                      body.instructions[i].offset);
      }
    }
    const auto bytes = method.allocated_bytes +
                       accumulated->allocated_bytes + bound->allocated_bytes +
                       analyzed->AllocatedBytes();
    return SemanticProof{std::move(*accumulated), std::move(*bound),
                         std::move(*analyzed),
                         bytes};
  } catch (const std::bad_alloc&) {
    return Fail(SemanticProofError::kResourceLimit);
  } catch (const std::length_error&) {
    return Fail(SemanticProofError::kResourceLimit);
  }
}
}  // namespace abcd::analysis
