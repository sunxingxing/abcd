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
bool IsSlotCopy(const analysis::SemanticOperation& operation) {
  const auto* assignment = std::get_if<analysis::ScalarAssignment>(&operation);
  if (!assignment || assignment->completion != analysis::CompletionEffect::kNormal)
    return false;
  const auto* value = std::get_if<analysis::ScalarValue>(&assignment->expression);
  return value && (std::holds_alternative<analysis::Accumulator>(*value) ||
                   std::holds_alternative<abc::RegisterId>(*value));
}
bool IsPrivateValue(const analysis::SemanticOperation& operation) {
  if (std::holds_alternative<analysis::StringLoad>(operation)) return true;
  const auto* assignment = std::get_if<analysis::ScalarAssignment>(&operation);
  if (!assignment || assignment->completion != analysis::CompletionEffect::kNormal)
    return false;
  const auto* value = std::get_if<analysis::ScalarValue>(&assignment->expression);
  // Constants/private slots have no observable evaluation. Do not widen this
  // to allocation, coercion, property reads or arbitrary scalar expressions.
  return value && !std::holds_alternative<analysis::EmptyArray>(*value);
}
bool KeepsObjectsPrivate(const analysis::SemanticOperation& operation) {
  if (IsPrivateValue(operation) ||
      std::holds_alternative<analysis::BufferedObjectCreation>(operation) ||
      std::holds_alternative<analysis::EmptyObjectCreation>(operation) ||
      std::holds_alternative<analysis::NoOperation>(operation)) return true;
  const auto* assignment = std::get_if<analysis::ScalarAssignment>(&operation);
  if (!assignment) return false;
  const auto* value = std::get_if<analysis::ScalarValue>(&assignment->expression);
  return value && std::holds_alternative<analysis::EmptyArray>(*value);
}
std::optional<std::size_t> Definition(
    const analysis::SemanticMethod& method,
    std::optional<analysis::SsaValueId> value) {
  if (!value) return {};
  const auto* origin = std::get_if<analysis::InstructionValue>(
      &method.ssa.Values()[value->value].origin);
  if (!origin) return {};
  return origin->instruction.value;
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
  struct CallInterval { std::size_t first; std::size_t last; };
  const auto available = budget_bytes - regions.allocated_bytes - proof_bytes;
  if (method.instructions.size() > available / sizeof(CallInterval))
    return Fail(SourceError::kResourceLimit);
  auto remaining = available - method.instructions.size() * sizeof(CallInterval);
  if (method.instructions.size() > remaining / sizeof(SourceStatement) ||
      method.flow.blocks.size() >
          (remaining - method.instructions.size() *
                           sizeof(SourceStatement)) /
              sizeof(std::optional<analysis::BlockId>))
    return Fail(SourceError::kResourceLimit);
  try {
    std::vector<CallInterval> call_roots;
    call_roots.reserve(method.instructions.size());
    if (call_roots.capacity() > available / sizeof(CallInterval))
      return Fail(SourceError::kResourceLimit);
    remaining = available - call_roots.capacity() * sizeof(CallInterval);
    if (method.instructions.size() > remaining / sizeof(SourceStatement) ||
        method.flow.blocks.size() >
            (remaining - method.instructions.size() * sizeof(SourceStatement)) /
                sizeof(std::optional<analysis::BlockId>))
      return Fail(SourceError::kResourceLimit);
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
    std::optional<std::size_t> last_effect;
    std::optional<std::size_t> last_object_effect;
    std::size_t block_id = 0;
    std::size_t module_fact = 0;
    std::size_t capture_fact = 0;
    for (const auto& instruction : method.instructions) {
      const auto position = ast.statements.size();
      while (block_id + 1 < method.flow.blocks.size() &&
             method.flow.blocks[block_id + 1].first_instruction <= position)
        ++block_id;
      const auto& effect = instruction.effects;
      SourceOperation operation;
      std::optional<abc::CodeOffset> fused_read;
      std::optional<abc::CodeOffset> fused_spill;
      std::optional<abc::CodeOffset> fused_reload;
      std::size_t read_distance = 0;
      std::size_t spill_distance = 0;
      std::size_t reload_distance = 0;
      std::optional<abc::CodeOffset> previous_origin;
      if (const auto* assignment =
              std::get_if<analysis::ScalarAssignment>(&instruction.operation)) {
        if (!SameSlot(effect.normal_registers.write,
                      assignment->destination))
          return Fail(SourceError::kInvalidEffect, instruction.pc);
        operation = SourceAssignment{assignment->destination,
                                     assignment->expression};
      } else if (std::holds_alternative<analysis::LexicalLoad>(
                     instruction.operation)) {
        if (capture_fact >= proof.captured_reads.size() ||
            proof.captured_reads[capture_fact].instruction.value != position)
          return Fail(SourceError::kUnsupportedOperation, instruction.pc);
        operation = SourceAssignment{
            analysis::ScalarSlot{analysis::Accumulator{}},
            CapturedExpression{proof.captured_reads[capture_fact++].cell}};
      } else if (const auto* string =
                     std::get_if<analysis::StringLoad>(&instruction.operation)) {
        if (!SameSlot(effect.normal_registers.write,
                      analysis::ScalarSlot{analysis::Accumulator{}}) ||
            effect.exceptional != analysis::ExceptionalCompletion::kImpossible)
          return Fail(SourceError::kInvalidEffect, instruction.pc);
        operation = SourceAssignment{
            analysis::ScalarSlot{analysis::Accumulator{}},
            StringExpression{string->value}};
      } else if (const auto* write =
                     std::get_if<analysis::OwnWrite>(&instruction.operation)) {
        const auto object = ReadValue(method, position,
                                       analysis::ScalarSlot{write->object});
        const auto origin = Definition(method, CopyInput(method, object));
        if (write->kind != analysis::OwnWriteKind::kDefineField || !origin ||
            *origin < method.flow.blocks[block_id].first_instruction ||
            (last_object_effect && *last_object_effect > *origin))
          return Fail(SourceError::kUnsupportedOperation, instruction.pc);
        const auto* buffered = std::get_if<analysis::BufferedObjectCreation>(
            &method.instructions[*origin].operation);
        if (!buffered)
          return Fail(SourceError::kUnsupportedOperation, instruction.pc);
        // Same-block private allocation, with only private value work and
        // already proven fresh-object writes since creation. The printer must
        // additionally prove this exact key is an existing writable data field.
        operation = SourceOwnStore{*write, *buffered};
      } else if (const auto* call =
                     std::get_if<analysis::ConstructorCall>(&instruction.operation)) {
        operation = SourceAssignment{
            analysis::ScalarSlot{analysis::Accumulator{}}, *call};
      } else if (const auto* global =
                     std::get_if<analysis::GlobalAccess>(&instruction.operation)) {
        if (global->operation != analysis::GlobalOp::kReadName)
          return Fail(SourceError::kUnsupportedOperation, instruction.pc);
        operation = SourceAssignment{
            analysis::ScalarSlot{analysis::Accumulator{}}, GlobalExpression{global->name}};
      } else if (const auto* empty =
                     std::get_if<analysis::EmptyObjectCreation>(&instruction.operation)) {
        operation = SourceAssignment{
            analysis::ScalarSlot{analysis::Accumulator{}}, *empty};
      } else if (const auto* buffered =
                     std::get_if<analysis::BufferedObjectCreation>(&instruction.operation)) {
        operation = SourceAssignment{
            analysis::ScalarSlot{analysis::Accumulator{}}, *buffered};
      } else if (const auto* module =
                     std::get_if<analysis::ModuleLoad>(&instruction.operation)) {
        if (module_fact >= proof.guarded_modules.size() ||
            proof.guarded_modules[module_fact].instruction.value != position)
          return Fail(SourceError::kUnsupportedOperation, instruction.pc);
        ++module_fact;
        operation = SourceAssignment{
            analysis::ScalarSlot{analysis::Accumulator{}}, ModuleExpression{*module}};
      } else if (std::holds_alternative<analysis::HoleGuard>(instruction.operation)) {
        if (!position || !module_fact ||
            proof.guarded_modules[module_fact - 1].instruction.value != position - 1)
          return Fail(SourceError::kUnsupportedOperation, instruction.pc);
        operation = analysis::NoOperation{};
        previous_origin = method.instructions[position - 1].pc;
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
        const auto callee = ReadValue(
            method, i, analysis::ScalarSlot{analysis::Accumulator{}});
        auto property_id = Definition(method, callee);
        std::optional<std::size_t> spill;
        std::optional<std::size_t> reload;
        if (!callee || uses[callee->value] != 1)
          return Fail(SourceError::kUnsupportedOperation, instruction.pc);
        if (property_id && IsSlotCopy(method.instructions[*property_id].operation)) {
          const auto saved = CopyInput(method, callee);
          const auto fetched = CopyInput(method, saved);
          spill = Definition(method, saved);
          reload = property_id;
          property_id = Definition(method, fetched);
          if (!saved || !fetched || !callee || !spill || !property_id ||
              uses[saved->value] != 1 || uses[fetched->value] != 1 ||
              uses[callee->value] != 1 || *property_id >= *spill ||
              *spill >= *reload || *reload >= i)
            return Fail(SourceError::kUnsupportedOperation, instruction.pc);
        }
        if (!property_id || !last_effect ||
            *property_id < method.flow.blocks[block_id].first_instruction)
          return Fail(SourceError::kUnsupportedOperation, instruction.pc);
        const auto property = *property_id;
        const auto& preceding = method.instructions[property];
        const auto* read = std::get_if<analysis::NamedRead>(&preceding.operation);
        const auto receiver = ReadValue(method, i, analysis::ScalarSlot{call->receiver});
        const auto object = ReadValue(
            method, property, analysis::ScalarSlot{analysis::Accumulator{}});
        if (!read || !object || !receiver ||
            (object != receiver && CopyInput(method, object) != receiver &&
             object != CopyInput(method, receiver)) || !callee)
          return Fail(SourceError::kUnsupportedOperation, instruction.pc);
        if (!SameSlot(effect.normal_registers.write,
                      analysis::ScalarSlot{analysis::Accumulator{}}) ||
            effect.exceptional != analysis::ExceptionalCompletion::kMayThrow)
          return Fail(SourceError::kInvalidEffect, instruction.pc);
        const bool staged = *last_effect != property;
        if (staged && (!spill || !reload || !call->Count()))
          return Fail(SourceError::kUnsupportedOperation, instruction.pc);
        if (staged) {
          // End-ordered interval stack: accept nesting, reject crossing. Each
          // completed interval is pushed/popped once, with no suffix scans.
          while (!call_roots.empty() && call_roots.back().last > property) {
            if (call_roots.back().first < property)
              return Fail(SourceError::kUnsupportedOperation, instruction.pc);
            call_roots.pop_back();
          }
          call_roots.push_back({property, i});
        }
        // Private slot copies cannot observe or change getter effects. The
        // last-effect index proves no effect is crossed in O(1) per call.
        ast.statements[property].operation = analysis::NoOperation{};
        if (spill && reload) {
          ast.statements[*spill].operation = analysis::NoOperation{};
          ast.statements[*reload].operation = analysis::NoOperation{};
          fused_spill = method.instructions[*spill].pc;
          fused_reload = method.instructions[*reload].pc;
          spill_distance = i - *spill;
          reload_distance = i - *reload;
        }
        if (staged) {
          ast.statements[property].operation = SourceStagedCall{
              read->name, *call,
              analysis::InstructionId{static_cast<std::uint32_t>(property + 1)},
              analysis::InstructionId{static_cast<std::uint32_t>(i)}};
          // Emission visits the interval through its owner, not twice through
          // the enclosing block. The original call PC receives its own span.
          operation = analysis::NoOperation{};
          fused_spill.reset();
          fused_reload.reset();
          spill_distance = 0;
          reload_distance = 0;
        } else {
          fused_read = preceding.pc;
          read_distance = i - property;
          operation = SourceAssignment{
              analysis::ScalarSlot{analysis::Accumulator{}},
              MemberCallExpression{read->name, *call}};
        }
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
                                fused_spill, fused_reload, read_distance,
                                spill_distance, reload_distance, previous_origin});
      if (!IsPrivateValue(instruction.operation)) last_effect = position;
      if (!KeepsObjectsPrivate(instruction.operation) &&
          !std::holds_alternative<SourceOwnStore>(ast.statements.back().operation))
        last_object_effect = position;
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
