#include "branch_source_facts.h"

#include <new>
#include <stdexcept>

#include "abcd/analysis/hole_guard.h"
#include "abcd/analysis/lexical.h"
#include "abcd/analysis/module_load.h"
#include "module_binding.h"

namespace abcd::source {
std::variant<BranchSourceFacts, BodyEmitFailure> BuildCapturedBranchFacts(
    const abc::MethodBody& body, std::uint64_t frame_size,
    const MethodConstants* constants, LocalLexicalBudget budget) {
  const auto fail = [](BodyEmitError kind, abc::CodeOffset offset = {}) {
    return BodyEmitFailure{kind, offset};
  };
  try {
    const auto built = analysis::BuildNormalFlow(body, {budget.bytes});
    if (const auto* error = std::get_if<analysis::FlowFailure>(&built))
      return fail(error->kind == analysis::FlowError::kResourceLimit
                      ? BodyEmitError::kResourceLimit
                      : BodyEmitError::kUnsupportedInstruction,
                  error->offset);
    const auto& graph = std::get<analysis::NormalFlow>(built);
    if (graph.blocks.capacity() >
        budget.bytes / sizeof(analysis::FlowBlock))
      return fail(BodyEmitError::kResourceLimit);
    budget.bytes -= graph.blocks.capacity() * sizeof(analysis::FlowBlock);
    const auto bound = analysis::AnalyzeEnvironments(
        body, graph, frame_size, {budget.bytes});
    if (const auto* error =
            std::get_if<analysis::EnvironmentFlowFailure>(&bound))
      return fail(error->kind == analysis::EnvironmentFlowError::kResourceLimit
                      ? BodyEmitError::kResourceLimit
                      : BodyEmitError::kUnsupportedInstruction,
                  error->offset);
    const auto& environments = std::get<analysis::EnvironmentFlow>(bound);
    if (environments.allocated_bytes > budget.bytes)
      return fail(BodyEmitError::kResourceLimit);
    budget.bytes -= environments.allocated_bytes;
    const auto modules = ValidateGuardedModuleLoads(
        body, constants, budget.bytes, budget.work);
    if (const auto* error = std::get_if<BodyEmitFailure>(&modules))
      return *error;
    const auto& admitted = std::get<GuardedModuleLoads>(modules);
    if (admitted.AllocatedBytes() > budget.bytes)
      return fail(BodyEmitError::kResourceLimit);
    budget.bytes -= admitted.AllocatedBytes();
    BranchSourceFacts result;
    if (body.instructions.size() > budget.bytes)
      return fail(BodyEmitError::kResourceLimit);
    result.language_loads.resize(body.instructions.size());
    if (result.language_loads.capacity() > budget.bytes)
      return fail(BodyEmitError::kResourceLimit);
    budget.bytes -= result.language_loads.capacity();
    if (environments.bindings.size() >
        budget.bytes / sizeof(CapturedLoadSite))
      return fail(BodyEmitError::kResourceLimit);
    result.captures.reserve(environments.bindings.size());
    if (result.captures.capacity() >
        budget.bytes / sizeof(CapturedLoadSite))
      return fail(BodyEmitError::kResourceLimit);
    budget.bytes -= result.captures.capacity() * sizeof(CapturedLoadSite);
    std::size_t binding = 0;
    std::size_t block = 0;
    for (std::size_t i = 0; i < body.instructions.size(); ++i) {
      while (block + 1 < graph.blocks.size() &&
             graph.blocks[block + 1].first_instruction <= i)
        ++block;
      const auto& instruction = body.instructions[i];
      const auto lexical = analysis::LiftLexicalInstruction(
          instruction.decoded, frame_size);
      if (std::holds_alternative<analysis::LexicalLoad>(lexical)) {
        if (binding >= environments.bindings.size() ||
            environments.bindings[binding].instruction != i)
          return fail(BodyEmitError::kUnsupportedInstruction,
                      instruction.offset);
        const auto& site = environments.bindings[binding++];
        const auto* depth =
            std::get_if<analysis::CapturedDepth>(&site.binding.environment);
        if (!depth)
          return fail(BodyEmitError::kUnsupportedInstruction,
                      instruction.offset);
        result.captures.push_back(
            {static_cast<std::uint32_t>(i), {*depth, site.binding.slot}});
        result.language_loads[i] = 1;
      } else if (!std::holds_alternative<analysis::LexicalError>(lexical) ||
                 std::get<analysis::LexicalError>(lexical) !=
                     analysis::LexicalError::kUnsupportedOpcode) {
        return fail(BodyEmitError::kUnsupportedInstruction,
                    instruction.offset);
      }
      if (!admitted.admitted.empty() && admitted.admitted[i]) {
        if (i + 1 < body.instructions.size()) {
          const auto guard = analysis::LiftHoleInstruction(
              body.instructions[i + 1].decoded, frame_size);
          if (const auto* checked = std::get_if<analysis::HoleGuard>(&guard);
              checked &&
              std::holds_alternative<analysis::Accumulator>(checked->value) &&
              std::holds_alternative<analysis::HoleNameSlot>(checked->name) &&
              i + 1 >= std::size_t(graph.blocks[block].first_instruction) +
                           graph.blocks[block].instruction_count)
            return fail(BodyEmitError::kModuleBinding, instruction.offset);
        }
        result.language_loads[i] = 1;
      }
    }
    if (binding != environments.bindings.size())
      return fail(BodyEmitError::kUnsupportedInstruction);
    std::size_t count = 0;
    for (const auto load : result.language_loads) count += load != 0;
    if (count > budget.bytes / sizeof(analysis::LanguageLoadFact))
      return fail(BodyEmitError::kResourceLimit);
    result.value_facts.reserve(count);
    if (result.value_facts.capacity() >
        budget.bytes / sizeof(analysis::LanguageLoadFact))
      return fail(BodyEmitError::kResourceLimit);
    for (std::size_t i = 0; i < result.language_loads.size(); ++i)
      if (result.language_loads[i])
        result.value_facts.push_back(
            {analysis::InstructionId{static_cast<std::uint32_t>(i)}});
    return result;
  } catch (const std::bad_alloc&) {
    return fail(BodyEmitError::kResourceLimit);
  } catch (const std::length_error&) {
    return fail(BodyEmitError::kResourceLimit);
  }
}
}  // namespace abcd::source
