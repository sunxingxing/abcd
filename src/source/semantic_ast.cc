#include "abcd/source/semantic_source.h"

#include <algorithm>
#include <new>
#include <stdexcept>
#include <utility>

namespace abcd::source {
namespace {
SourceFailure Fail(SourceError kind, abc::CodeOffset pc = {}) {
  return {kind, pc};
}
bool SameSlot(const std::optional<analysis::ScalarSlot>& actual,
              const analysis::ScalarSlot& expected) {
  if (!actual || actual->index() != expected.index()) return false;
  if (const auto* register_id = std::get_if<abc::RegisterId>(&expected))
    return std::get<abc::RegisterId>(*actual) == *register_id;
  return true;
}
std::optional<analysis::BlockId> Target(const analysis::NormalFlow& flow,
                                        abc::CodeOffset pc) {
  const auto block = std::lower_bound(
      flow.blocks.begin(), flow.blocks.end(), pc,
      [](const analysis::FlowBlock& item, abc::CodeOffset target) {
        return item.begin < target;
      });
  if (block == flow.blocks.end() || block->begin != pc)
    return std::nullopt;
  return analysis::BlockId{
      static_cast<std::uint32_t>(block - flow.blocks.begin())};
}
}  // namespace

SourceAstResult BuildSourceAst(const analysis::SemanticMethod& method,
                               const analysis::SemanticProof& proof,
                               analysis::SemanticRegionTree regions,
                               std::size_t budget_bytes) {
  if (proof.accumulator.entries.size() != method.flow.blocks.size() ||
      proof.environments.before.size() != method.instructions.size() ||
      proof.values.Facts().size() != method.ssa.Values().size())
    return Fail(SourceError::kInvalidEffect);
  if (regions.allocated_bytes > budget_bytes)
    return Fail(SourceError::kResourceLimit);
  const auto remaining = budget_bytes - regions.allocated_bytes;
  if (method.instructions.size() > remaining / sizeof(SourceStatement) ||
      method.flow.blocks.size() >
          (remaining - method.instructions.size() *
                           sizeof(SourceStatement)) /
              sizeof(std::optional<analysis::BlockId>))
    return Fail(SourceError::kResourceLimit);
  try {
    SourceAst ast{std::move(regions)};
    ast.statements.reserve(method.instructions.size());
    ast.fallthrough.resize(method.flow.blocks.size());
    if (ast.statements.capacity() > remaining / sizeof(SourceStatement) ||
        ast.fallthrough.capacity() >
            (remaining - ast.statements.capacity() *
                             sizeof(SourceStatement)) /
                sizeof(std::optional<analysis::BlockId>))
      return Fail(SourceError::kResourceLimit);
    for (std::size_t i = 0; i < method.flow.blocks.size(); ++i) {
      const auto& block = method.flow.blocks[i];
      for (std::size_t edge = 0; edge < block.successor_count; ++edge)
        if (block.transfer != abc::ControlTransfer::kJump &&
            block.successors[edge].kind ==
            analysis::FlowEdgeKind::kFallthrough)
          ast.fallthrough[i] = block.successors[edge].target;
    }
    for (const auto& instruction : method.instructions) {
      const auto& effect = instruction.effects;
      SourceOperation operation;
      if (const auto* assignment =
              std::get_if<analysis::ScalarAssignment>(&instruction.operation)) {
        if (!SameSlot(effect.normal_registers.write,
                      assignment->destination))
          return Fail(SourceError::kInvalidEffect, instruction.pc);
        operation = SourceAssignment{assignment->destination,
                                     assignment->expression};
      } else if (const auto* string =
                     std::get_if<analysis::StringLoad>(&instruction.operation)) {
        if (!SameSlot(effect.normal_registers.write,
                      analysis::ScalarSlot{analysis::Accumulator{}}) ||
            effect.exceptional != analysis::ExceptionalCompletion::kImpossible)
          return Fail(SourceError::kInvalidEffect, instruction.pc);
        operation = SourceAssignment{
            analysis::ScalarSlot{analysis::Accumulator{}},
            StringExpression{string->value}};
      } else if (const auto* read =
                     std::get_if<analysis::NamedRead>(&instruction.operation)) {
        if (!SameSlot(effect.normal_registers.write,
                      analysis::ScalarSlot{analysis::Accumulator{}}) ||
            effect.exceptional !=
                analysis::ExceptionalCompletion::kMayThrow)
          return Fail(SourceError::kInvalidEffect, instruction.pc);
        operation = SourceAssignment{
            analysis::ScalarSlot{analysis::Accumulator{}},
            MemberExpression{analysis::ScalarValue{analysis::Accumulator{}},
                             read->name}};
      } else if (const auto* write =
                     std::get_if<analysis::NamedWrite>(&instruction.operation)) {
        if (effect.normal_registers.write ||
            effect.exceptional !=
                analysis::ExceptionalCompletion::kMayThrow)
          return Fail(SourceError::kInvalidEffect, instruction.pc);
        operation = SourceMemberStore{
            analysis::ScalarValue{write->object}, write->name,
            analysis::ScalarValue{analysis::Accumulator{}}};
      } else if (const auto* branch =
                     std::get_if<analysis::BranchOperation>(
                         &instruction.operation)) {
        const auto target = Target(method.flow, branch->target);
        if (!target) return Fail(SourceError::kMissingTarget, instruction.pc);
        operation = SourceConditionalBreak{branch->predicate, *target};
      } else if (const auto* jump =
                     std::get_if<analysis::JumpOperation>(
                         &instruction.operation)) {
        const auto target = Target(method.flow, jump->target);
        if (!target) return Fail(SourceError::kMissingTarget, instruction.pc);
        operation = SourceBreak{*target};
      } else if (const auto* returned =
                     std::get_if<analysis::ReturnOperation>(
                         &instruction.operation)) {
        operation = SourceReturn{returned->value};
      } else if (std::holds_alternative<analysis::NoOperation>(
                     instruction.operation)) {
        operation = analysis::NoOperation{};
      } else {
        return Fail(SourceError::kUnsupportedOperation, instruction.pc);
      }
      ast.statements.push_back({instruction.pc, std::move(operation)});
    }
    ast.allocated_bytes = ast.regions.allocated_bytes +
                          ast.statements.capacity() * sizeof(SourceStatement) +
                          ast.fallthrough.capacity() *
                              sizeof(std::optional<analysis::BlockId>);
    return ast;
  } catch (const std::bad_alloc&) {
    return Fail(SourceError::kResourceLimit);
  } catch (const std::length_error&) {
    return Fail(SourceError::kResourceLimit);
  }
}
}  // namespace abcd::source
