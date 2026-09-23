#include "branch_value_proof.h"

#include <vector>
#include <stdexcept>

#include "abcd/analysis/register_value_flow.h"

namespace abcd::source {
std::optional<BodyEmitFailure> ProveBranchValues(
    const abc::MethodBody& body, std::uint64_t frame_size,
    LocalLexicalBudget budget) {
  using namespace analysis;
  const auto fail = [](BodyEmitError kind, abc::CodeOffset offset = {}) {
    return BodyEmitFailure{kind, offset};
  };
  try {
    const auto built = BuildNormalFlow(body, {budget.bytes});
    if (const auto* error = std::get_if<FlowFailure>(&built))
      return fail(error->kind == FlowError::kResourceLimit
                      ? BodyEmitError::kResourceLimit
                      : BodyEmitError::kUnsupportedInstruction,
                  error->offset);
    const auto& graph = std::get<NormalFlow>(built);
    if (graph.blocks.capacity() > budget.bytes / sizeof(FlowBlock))
      return fail(BodyEmitError::kResourceLimit);
    budget.bytes -= graph.blocks.capacity() * sizeof(FlowBlock);
    IdfQuota quota{budget.work};
    const auto named = RegisterSsa::Build(body, graph, frame_size, quota,
                                          {budget.bytes});
    if (const auto* error = std::get_if<RegisterUseFailure>(&named))
      return fail(error->kind == RegisterUseError::kWorkLimit
                      ? BodyEmitError::kWorkLimit
                      : error->kind == RegisterUseError::kResourceLimit
                            ? BodyEmitError::kResourceLimit
                            : BodyEmitError::kUnsupportedInstruction,
                  error->offset);
    if (const auto* error = std::get_if<IdfError>(&named))
      return fail(*error == IdfError::kWorkLimit
                      ? BodyEmitError::kWorkLimit
                      : *error == IdfError::kResourceLimit
                            ? BodyEmitError::kResourceLimit
                            : BodyEmitError::kUnsupportedInstruction);
    if (const auto* error = std::get_if<DominatorError>(&named))
      return fail(*error == DominatorError::kResourceLimit
                      ? BodyEmitError::kResourceLimit
                      : BodyEmitError::kUnsupportedInstruction);
    if (const auto* error = std::get_if<SsaError>(&named))
      return fail(*error == SsaError::kWorkLimit
                      ? BodyEmitError::kWorkLimit
                      : BodyEmitError::kResourceLimit);
    const auto& ssa = std::get<RegisterSsa>(named);
    if (ssa.AllocatedBytes() > budget.bytes)
      return fail(BodyEmitError::kResourceLimit);
    budget.bytes -= ssa.AllocatedBytes();
    if (ssa.Values().size() > budget.bytes / sizeof(EntryValueFact))
      return fail(BodyEmitError::kResourceLimit);
    std::vector<EntryValueFact> entries;
    entries.reserve(ssa.Values().size());
    if (entries.capacity() > budget.bytes / sizeof(EntryValueFact))
      return fail(BodyEmitError::kResourceLimit);
    for (std::size_t i = 0; i < ssa.Values().size(); ++i) {
      const auto& value = ssa.Values()[i];
      if (std::holds_alternative<MethodEntry>(value.origin) &&
          std::holds_alternative<abc::RegisterId>(value.slot))
        entries.push_back({SsaValueId{static_cast<std::uint32_t>(i)},
                           ValueKinds::Language()});
    }
    budget.bytes -= entries.capacity() * sizeof(EntryValueFact);
    const auto analyzed = RegisterValueFlow::Build(
        body, ssa, frame_size, entries, quota, {budget.bytes});
    if (const auto* error = std::get_if<ValueFlowFailure>(&analyzed))
      return fail(error->kind == ValueFlowError::kWorkLimit
                      ? BodyEmitError::kWorkLimit
                      : error->kind == ValueFlowError::kResourceLimit
                            ? BodyEmitError::kResourceLimit
                            : BodyEmitError::kUnsupportedInstruction,
                  error->instruction &&
                          error->instruction->value < body.instructions.size()
                      ? body.instructions[error->instruction->value].offset
                      : abc::CodeOffset{});
    const auto& values = std::get<RegisterValueFlow>(analyzed);
    for (std::size_t i = 0; i < body.instructions.size(); ++i) {
      const auto id = InstructionId{static_cast<std::uint32_t>(i)};
      for (const auto& read : ssa.Reads(id)) {
        const auto kinds = values.At(read.value);
        if (!kinds || !kinds->IsLanguage())
          return fail(BodyEmitError::kInternalValue,
                      body.instructions[i].offset);
      }
    }
    return {};
  } catch (const std::bad_alloc&) {
    return fail(BodyEmitError::kResourceLimit);
  } catch (const std::length_error&) {
    return fail(BodyEmitError::kResourceLimit);
  }
}
}  // namespace abcd::source
