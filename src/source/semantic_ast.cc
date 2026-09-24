#include "abcd/source/semantic_source.h"

#include <algorithm>
#include <new>
#include <stdexcept>
#include <utility>

#include "semantic_objects.h"

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
                               std::size_t budget_bytes,
                               std::span<const std::uint8_t> named_scopes,
                               std::optional<analysis::RestPrologue> rest) {
  if (proof.environments.before.size() != method.instructions.size() ||
      proof.values.Facts().size() != method.ssa.Values().size())
    return Fail(SourceError::kInvalidEffect);
  if (!named_scopes.empty() && named_scopes.size() != method.instructions.size())
    return Fail(SourceError::kInvalidEffect);
  if (proof.locals.cells.size() > budget_bytes / sizeof(analysis::LocalCell))
    return Fail(SourceError::kResourceLimit);
  const auto local_bytes = proof.locals.cells.size() * sizeof(analysis::LocalCell);
  budget_bytes -= local_bytes;
  if (regions.allocated_bytes > budget_bytes)
    return Fail(SourceError::kResourceLimit);
  constexpr auto kProofValueBytes =
      sizeof(std::uint8_t) + sizeof(PrivateObjects::Origin);
  if (method.ssa.Values().size() >
      (budget_bytes - regions.allocated_bytes) / kProofValueBytes)
    return Fail(SourceError::kResourceLimit);
  const auto proof_bytes = method.ssa.Values().size() * kProofValueBytes;
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
    PrivateObjects objects(method.ssa.Values().size());
    if (objects.AllocatedBytes() >
        method.ssa.Values().size() * sizeof(PrivateObjects::Origin))
      return Fail(SourceError::kResourceLimit);
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
    ast.local_cells = proof.locals.cells;
    if (ast.local_cells.capacity() > proof.locals.cells.size())
      return Fail(SourceError::kResourceLimit);
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
    std::size_t block_id = 0;
    std::size_t module_fact = 0;
    std::size_t capture_fact = 0;
    std::size_t local_fact = 0;
    std::optional<analysis::InstructionId> previous_creation;
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
      if (local_fact < proof.locals.accesses.size() &&
          proof.locals.accesses[local_fact].instruction.value == position) {
        const auto& access = proof.locals.accesses[local_fact++];
        if (access.cell.value >= ast.local_cells.size())
          return Fail(SourceError::kInvalidEffect, instruction.pc);
        if (access.kind == analysis::LocalAccessKind::kLoad) {
          if (!std::holds_alternative<analysis::LexicalLoad>(instruction.operation))
            return Fail(SourceError::kInvalidEffect, instruction.pc);
          operation = SourceAssignment{analysis::ScalarSlot{analysis::Accumulator{}},
                                       LocalExpression{access.cell}};
        } else {
          const auto* store = std::get_if<analysis::LexicalStore>(&instruction.operation);
          if (!store) return Fail(SourceError::kInvalidEffect, instruction.pc);
          operation = SourceLocalStore{access.cell, store->value};
        }
      } else if (const auto* environment =
                     std::get_if<analysis::PushEnvironment>(&instruction.operation)) {
        if (environment->scope && (named_scopes.empty() || !named_scopes[position]))
          return Fail(SourceError::kConstant, instruction.pc);
        // SSA rejects every use of the internal env result; topology is
        // separately checked. Omission follows the existing no-debugger/OOM
        // observability contract, never a Hole -> undefined substitution.
        operation = analysis::NoOperation{};
      } else if (std::holds_alternative<analysis::PopEnvironment>(instruction.operation)) {
        operation = analysis::NoOperation{};
      } else if (const auto* assignment =
              std::get_if<analysis::ScalarAssignment>(&instruction.operation)) {
        if (!SameSlot(effect.normal_registers.write,
                      assignment->destination))
          return Fail(SourceError::kInvalidEffect, instruction.pc);
        operation = SourceAssignment{assignment->destination,
                                     assignment->expression};
      } else if (const auto* created =
                     std::get_if<analysis::FunctionCreation>(&instruction.operation)) {
        if (created->kind != analysis::CreationKind::kFunction ||
            created->home_object != analysis::HomeObjectInput::kCurrentFunction ||
            !SameSlot(effect.normal_registers.write,
                      analysis::ScalarSlot{analysis::Accumulator{}}))
          return Fail(SourceError::kUnsupportedOperation, instruction.pc);
        // Target syntax/ABI and absence of external captures are verified by
        // the callable owner before this expression is published.
        operation = SourceAssignment{
            analysis::ScalarSlot{analysis::Accumulator{}},
            FunctionExpression{*created,
                analysis::InstructionId{static_cast<std::uint32_t>(position)},
                previous_creation}};
        previous_creation = analysis::InstructionId{static_cast<std::uint32_t>(position)};
      } else if (std::holds_alternative<analysis::LexicalLoad>(
                     instruction.operation)) {
        if (capture_fact >= proof.captured_reads.size() ||
            proof.captured_reads[capture_fact].instruction.value != position)
          return Fail(SourceError::kUnsupportedOperation, instruction.pc);
        operation = SourceAssignment{
            analysis::ScalarSlot{analysis::Accumulator{}},
            CapturedExpression{proof.captured_reads[capture_fact++].cell}};
      } else if (const auto* copy =
                     std::get_if<analysis::RestCopy>(&instruction.operation)) {
        if (!rest || rest->first_argument != copy->first_argument)
          return Fail(SourceError::kUnsupportedOperation, instruction.pc);
        operation = SourceAssignment{
            analysis::ScalarSlot{analysis::Accumulator{}}, *copy};
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
        const auto origin = objects.Find(
            object, method.flow.blocks[block_id].first_instruction);
        if (write->kind != analysis::OwnWriteKind::kDefineField || !origin)
          return Fail(SourceError::kUnsupportedOperation, instruction.pc);
        const auto* buffered = std::get_if<analysis::BufferedObjectCreation>(
            &method.instructions[origin->value].operation);
        if (!buffered)
          return Fail(SourceError::kUnsupportedOperation, instruction.pc);
        // Unpublished same-block allocation, possibly across unrelated user
        // effects. The printer must still prove an existing writable data key.
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
      } else if (const auto* array =
                     std::get_if<analysis::BufferedArrayCreation>(&instruction.operation)) {
        operation = SourceAssignment{
            analysis::ScalarSlot{analysis::Accumulator{}}, *array};
      } else if (const auto* module =
                     std::get_if<analysis::ModuleLoad>(&instruction.operation)) {
        if (module_fact >= proof.module_reads.size() ||
            proof.module_reads[module_fact].instruction.value != position)
          return Fail(SourceError::kUnsupportedOperation, instruction.pc);
        ++module_fact;
        operation = SourceAssignment{
            analysis::ScalarSlot{analysis::Accumulator{}}, ModuleExpression{*module}};
      } else if (const auto* check =
                     std::get_if<analysis::SuperCheck>(&instruction.operation)) {
        switch (check->kind) {
          case analysis::SuperCheckKind::kRequireInitialized: {
            const auto value = ReadValue(method, position,
                analysis::ScalarSlot{analysis::Accumulator{}});
            const auto kinds = value ? proof.values.At(*value) : std::nullopt;
            // v7 RuntimeThrowIfSuperNotCorrectCall flag 0 rejects BOTH Hole
            // and undefined. A Language-only capture premise is insufficient.
            // Only these existing SSA refinements exclude both values; broader
            // this/closure initialization needs its own proof or explicit throw.
            if (!kinds || (!kinds->IsString() && !kinds->IsNonNaNNumber()))
              return Fail(SourceError::kSuperInitialization, instruction.pc);
            operation = analysis::NoOperation{};
            break;
          }
          case analysis::SuperCheckKind::kNoop:
            operation = analysis::NoOperation{};
            break;
          case analysis::SuperCheckKind::kRequireUninitialized:
            return Fail(SourceError::kSuperInitialization, instruction.pc);
        }
      } else if (const auto* guard =
                     std::get_if<analysis::HoleGuard>(&instruction.operation)) {
        const auto value = ReadValue(method, position, guard->value);
        const auto kinds = value ? proof.values.At(*value) : std::nullopt;
        if (!kinds || !kinds->IsLanguage())
          return Fail(SourceError::kInvalidEffect, instruction.pc);
        // Pinned UNDEFINEDIFHOLE handlers do not even read the error name on
        // the non-Hole path. Unlike SuperCheck, undefined passes this guard.
        // Captured facts remain conditional obligations of the whole template.
        operation = analysis::NoOperation{};
        if (position && module_fact &&
            proof.module_reads[module_fact - 1].proof == analysis::ModuleReadProof::kGuarded &&
            proof.module_reads[module_fact - 1].instruction.value == position - 1)
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
      objects.Observe(method,
          analysis::InstructionId{static_cast<std::uint32_t>(position)},
          method.flow.blocks[block_id].first_instruction,
          std::holds_alternative<SourceOwnStore>(ast.statements.back().operation));
    }
    ast.allocated_bytes = ast.regions.allocated_bytes + local_bytes +
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
