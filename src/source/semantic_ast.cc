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
std::optional<analysis::SsaValueId> ReadValue(
    const analysis::SemanticMethod& method, std::size_t instruction,
    const analysis::ScalarSlot& slot) {
  for (const auto& read : method.ssa.Reads(
           analysis::InstructionId{static_cast<std::uint32_t>(instruction)})) {
    if (SameSlot(method.ssa.Values()[read.value.value].slot, slot))
      return read.value;
  }
  return std::nullopt;
}
std::optional<analysis::SsaValueId> CopyInput(
    const analysis::SemanticMethod& method,
    std::optional<analysis::SsaValueId> value) {
  if (!value) return {};
  const auto* origin = std::get_if<analysis::InstructionValue>(
      &method.ssa.Values()[value->value].origin);
  if (!origin) return value;
  const auto* assignment = std::get_if<analysis::ScalarAssignment>(
      &method.instructions[origin->instruction.value].operation);
  if (!assignment) return value;
  const auto* input = std::get_if<analysis::ScalarValue>(&assignment->expression);
  if (!input) return value;
  if (const auto* reg = std::get_if<abc::RegisterId>(input))
    return ReadValue(method, origin->instruction.value, analysis::ScalarSlot{*reg});
  if (std::holds_alternative<analysis::Accumulator>(*input))
    return ReadValue(method, origin->instruction.value,
                     analysis::ScalarSlot{analysis::Accumulator{}});
  return value;
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
  const auto proof_bytes = method.ssa.Values().size() * sizeof(std::uint8_t);
  if (proof_bytes > budget_bytes - regions.allocated_bytes)
    return Fail(SourceError::kResourceLimit);
  const auto remaining = budget_bytes - regions.allocated_bytes - proof_bytes;
  if (method.instructions.size() > remaining / sizeof(SourceStatement) ||
      method.flow.blocks.size() >
          (remaining - method.instructions.size() *
                           sizeof(SourceStatement)) /
              sizeof(std::optional<analysis::BlockId>))
    return Fail(SourceError::kResourceLimit);
  try {
    // Saturating use counts prove a saved callee is unobservable outside its
    // reload. Build once, not a suffix scan for every call.
    std::vector<std::uint8_t> uses(method.ssa.Values().size());
    constexpr std::uint8_t kMultipleUses = 2;
    for (std::size_t i = 0; i < method.instructions.size(); ++i)
      for (const auto& read : method.ssa.Reads(analysis::InstructionId{
               static_cast<std::uint32_t>(i)}))
        if (uses[read.value.value] < kMultipleUses) ++uses[read.value.value];
    for (std::size_t i = 0; i < method.ssa.Values().size(); ++i)
      if (std::holds_alternative<analysis::PhiValue>(method.ssa.Values()[i].origin))
        for (const auto& input : method.ssa.Inputs(
                 analysis::SsaValueId{static_cast<std::uint32_t>(i)}))
          if (uses[input.value.value] < kMultipleUses) ++uses[input.value.value];
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
      std::optional<abc::CodeOffset> fused_read;
      std::optional<abc::CodeOffset> fused_spill;
      std::optional<abc::CodeOffset> fused_reload;
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
      } else if (const auto* call =
                     std::get_if<analysis::OrdinaryCall>(&instruction.operation)) {
        if (!SameSlot(effect.normal_registers.write,
                      analysis::ScalarSlot{analysis::Accumulator{}}) ||
            effect.exceptional != analysis::ExceptionalCompletion::kMayThrow)
          return Fail(SourceError::kInvalidEffect, instruction.pc);
        operation = SourceAssignment{
            analysis::ScalarSlot{analysis::Accumulator{}}, CallExpression{*call}};
      } else if (const auto* call =
                     std::get_if<analysis::ReceiverCall>(&instruction.operation)) {
        const auto i = ast.statements.size();
        if (!i || Target(method.flow, instruction.pc))
          return Fail(SourceError::kUnsupportedOperation, instruction.pc);
        std::size_t property = i - 1;
        constexpr std::size_t kSpilledPrefix = 3;
        if (!std::holds_alternative<analysis::NamedRead>(
                method.instructions[property].operation) && i >= kSpilledPrefix) {
          const auto saved = method.ssa.Write(analysis::InstructionId{
              static_cast<std::uint32_t>(i - 2)});
          const auto loaded = method.ssa.Write(analysis::InstructionId{
              static_cast<std::uint32_t>(i - 1)});
          const auto fetched = method.ssa.Write(analysis::InstructionId{
              static_cast<std::uint32_t>(i - kSpilledPrefix)});
          if (saved && loaded && fetched && uses[saved->value] == 1 &&
              uses[fetched->value] == 1 &&
              CopyInput(method, loaded) == saved &&
              CopyInput(method, saved) == fetched &&
              !Target(method.flow, method.instructions[i - 1].pc) &&
              !Target(method.flow, method.instructions[i - 2].pc)) {
            property = i - kSpilledPrefix;
            fused_spill = method.instructions[i - 2].pc;
            fused_reload = method.instructions[i - 1].pc;
          }
        }
        const auto& preceding = method.instructions[property];
        const auto* read = std::get_if<analysis::NamedRead>(&preceding.operation);
        const auto receiver = ReadValue(method, i, analysis::ScalarSlot{call->receiver});
        const auto object = ReadValue(
            method, property, analysis::ScalarSlot{analysis::Accumulator{}});
        const auto callee = ReadValue(
            method, i, analysis::ScalarSlot{analysis::Accumulator{}});
        if (!read || !object || !receiver ||
            (object != receiver && CopyInput(method, object) != receiver &&
             object != CopyInput(method, receiver)) || !callee ||
            callee != method.ssa.Write(analysis::InstructionId{
                          static_cast<std::uint32_t>(i - 1)}))
          return Fail(SourceError::kUnsupportedOperation, instruction.pc);
        if (!SameSlot(effect.normal_registers.write,
                      analysis::ScalarSlot{analysis::Accumulator{}}) ||
            effect.exceptional != analysis::ExceptionalCompletion::kMayThrow)
          return Fail(SourceError::kInvalidEffect, instruction.pc);
        // Same block and receiver identity; only dead callee copies may
        // intervene. Retain the getter once, before arguments. Never emit .call.
        for (std::size_t elided = property; elided < i; ++elided)
          ast.statements[elided].operation = analysis::NoOperation{};
        fused_read = preceding.pc;
        operation = SourceAssignment{
            analysis::ScalarSlot{analysis::Accumulator{}},
            MemberCallExpression{read->name, *call}};
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
      ast.statements.push_back({instruction.pc, std::move(operation), fused_read,
                                fused_spill, fused_reload});
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
