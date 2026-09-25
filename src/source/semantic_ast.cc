#include "abcd/source/semantic_source.h"

#include <algorithm>
#include <new>
#include <stdexcept>
#include <utility>

#include "semantic_objects.h"
#include "argument_region.h"
#include "semantic_ast_input.h"
#include "dead_copies.h"
#include "copy_origins.h"
#include "array_literals.h"
#include "class_assembly.h"

namespace abcd::source {
namespace {
abc::CodeOffset SourcePc(const analysis::SemanticInstruction& instruction) {
  return instruction.origin.value_or(instruction.pc);
}
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
    const AstMethod& method, std::size_t instruction,
    const analysis::ScalarSlot& slot) {
  for (const auto& read : method.ssa.Reads(
           analysis::InstructionId{static_cast<std::uint32_t>(instruction)})) {
    if (SameSlot(method.ssa.Slot(read.value), slot))
      return read.value;
  }
  return std::nullopt;
}
std::optional<analysis::SsaValueId> CopyInput(
    const AstMethod& method,
    std::optional<analysis::SsaValueId> value) {
  if (!value) return {};
  const auto origin = method.ssa.Definition(*value);
  if (!origin) return value;
  const auto* assignment = std::get_if<analysis::ScalarAssignment>(
      &method.instructions[origin->value].operation);
  if (!assignment) return value;
  const auto* input = std::get_if<analysis::ScalarValue>(&assignment->expression);
  if (!input) return value;
  if (const auto* reg = std::get_if<abc::RegisterId>(input))
    return ReadValue(method, origin->value, analysis::ScalarSlot{*reg});
  if (std::holds_alternative<analysis::Accumulator>(*input))
    return ReadValue(method, origin->value,
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
    const AstMethod& method,
    std::optional<analysis::SsaValueId> value) {
  if (!value) return {};
  const auto origin = method.ssa.Definition(*value);
  if (!origin) return {};
  return origin->value;
}
}  // namespace

SourceAstResult BuildSourceAst(const AstMethod& method,
                               const AstProof& proof,
                               analysis::SemanticRegionTree regions,
                               std::size_t budget_bytes,
                               std::span<const std::uint8_t> named_scopes,
                               std::optional<analysis::RestPrologue> rest,
                               std::optional<analysis::RangeDerivedConstructor> derived) {
  if (proof.instructions != method.instructions.size() ||
      proof.values.size() != method.ssa.ValueCount() ||
      regions.order_positions.size() != method.flow.blocks.size())
    return Fail(SourceError::kInvalidEffect);
  if (derived && method.ssa.IsPhased())
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
      sizeof(std::size_t) + sizeof(PrivateObjects::Origin) +
      sizeof(analysis::SsaValueId);
  if (method.ssa.ValueCount() >
      (budget_bytes - regions.allocated_bytes) / kProofValueBytes)
    return Fail(SourceError::kResourceLimit);
  const auto proof_bytes = method.ssa.ValueCount() * kProofValueBytes;
  if (proof_bytes > budget_bytes - regions.allocated_bytes)
    return Fail(SourceError::kResourceLimit);
  struct CallInterval { std::size_t first; std::size_t last; };
  auto available = budget_bytes - regions.allocated_bytes - proof_bytes;
  if (method.instructions.size() > available / PrivateObjects::kAllocationBytes)
    return Fail(SourceError::kResourceLimit);
  const auto allocation_bytes =
      method.instructions.size() * PrivateObjects::kAllocationBytes;
  available -= allocation_bytes;
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
    CopyOrigins copies(method.ssa.ValueCount());
    if (copies.AllocatedBytes() >
        method.ssa.ValueCount() * sizeof(analysis::SsaValueId))
      return Fail(SourceError::kResourceLimit);
    PrivateObjects objects(method.ssa.ValueCount(), method.instructions.size());
    if (objects.AllocatedBytes() >
        method.ssa.ValueCount() * sizeof(PrivateObjects::Origin) + allocation_bytes)
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
    // Exact counts also support sparse dead-copy removal after call fusion.
    // Build once, including exceptional phi uses, never scan suffixes per call.
    std::vector<std::size_t> uses(method.ssa.ValueCount());
    for (std::size_t i = 0; i < method.instructions.size(); ++i)
      for (const auto& read : method.ssa.Reads(analysis::InstructionId{
               static_cast<std::uint32_t>(i)}))
        ++uses[read.value.value];
    for (std::size_t i = 0; i < method.ssa.ValueCount(); ++i)
      if (method.ssa.IsPhi(analysis::SsaValueId{static_cast<std::uint32_t>(i)}))
        for (const auto& input : method.ssa.Inputs(
                 analysis::SsaValueId{static_cast<std::uint32_t>(i)}))
          ++uses[input.value.value];
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
      } else if (const auto* awaited =
                     std::get_if<analysis::AwaitValue>(&instruction.operation)) {
        if (!SameSlot(effect.normal_registers.write,
                      analysis::ScalarSlot{analysis::Accumulator{}}))
          return Fail(SourceError::kInvalidEffect, instruction.pc);
        operation = SourceAssignment{analysis::Accumulator{}, *awaited};
      } else if (const auto* created =
                     std::get_if<analysis::FunctionCreation>(&instruction.operation)) {
        if (!SameSlot(effect.normal_registers.write,
                      analysis::ScalarSlot{analysis::Accumulator{}}))
          return Fail(SourceError::kInvalidEffect, instruction.pc);
        const auto id = analysis::InstructionId{
            static_cast<std::uint32_t>(position)};
        switch (created->kind) {
          case analysis::CreationKind::kMethod:
            if (created->home_object != analysis::HomeObjectInput::kAccumulator)
              return Fail(SourceError::kInvalidEffect, instruction.pc);
            operation = SourceAssignment{
                analysis::ScalarSlot{analysis::Accumulator{}},
                MethodExpression{*created, id, {}}};
            break;
          case analysis::CreationKind::kFunction:
            if (created->home_object != analysis::HomeObjectInput::kCurrentFunction)
              return Fail(SourceError::kInvalidEffect, instruction.pc);
            // The callable owner verifies target syntax/ABI and discharges
            // every capture requirement before publishing this expression.
            operation = SourceAssignment{
                analysis::ScalarSlot{analysis::Accumulator{}},
                FunctionExpression{*created, id, previous_creation,
                                   method.ssa.Before(id)}};
            break;
        }
        previous_creation = id;
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
        if (effect.normal_registers.write ||
            effect.exceptional != analysis::ExceptionalCompletion::kMayThrow)
          return Fail(SourceError::kInvalidEffect, instruction.pc);
        const auto object = ReadValue(method, position,
                                       analysis::ScalarSlot{write->object});
        if (write->kind == analysis::OwnWriteKind::kStore) {
          const auto value = ReadValue(method, position,
              analysis::ScalarSlot{analysis::Accumulator{}});
          const auto creation = value ? method.ssa.Definition(copies.At(*value))
                                      : std::nullopt;
          const auto block_begin = method.flow.blocks[block_id].first_instruction;
          if (method.ssa.IsPhased() || !object || !creation ||
              creation->value < block_begin || creation->value >= position)
            return Fail(SourceError::kUnsupportedOperation, instruction.pc);
          const auto* created = std::get_if<analysis::FunctionCreation>(
              &method.instructions[creation->value].operation);
          const auto home = ReadValue(method, creation->value,
              analysis::ScalarSlot{analysis::Accumulator{}});
          if (!created || created->kind != analysis::CreationKind::kMethod ||
              created->home_object != analysis::HomeObjectInput::kAccumulator ||
              !home || copies.At(*home) != copies.At(*object))
            return Fail(SourceError::kUnsupportedOperation, instruction.pc);
          operation = SourceMethodStore{*write, *creation, copies.At(*home)};
        } else if (write->kind == analysis::OwnWriteKind::kDefineField) {
          const auto origin = objects.Find(
              object, method.flow.blocks[block_id].first_instruction);
          if (!origin)
            return Fail(SourceError::kUnsupportedOperation, instruction.pc);
          const auto* buffered = std::get_if<analysis::BufferedObjectCreation>(
              &method.instructions[origin->value].operation);
          if (!buffered)
            return Fail(SourceError::kUnsupportedOperation, instruction.pc);
          // Unpublished same-block allocation, possibly across unrelated user
          // effects. The printer still proves an existing writable data key.
          operation = SourceOwnStore{*write, *buffered};
        } else {
          return Fail(SourceError::kUnsupportedOperation, instruction.pc);
        }
      } else if (const auto* write =
                     std::get_if<analysis::IndexedWrite>(&instruction.operation)) {
        if (effect.normal_registers.write ||
            effect.exceptional != analysis::ExceptionalCompletion::kMayThrow)
          return Fail(SourceError::kInvalidEffect, instruction.pc);
        operation = SourceIndexedStore{*write, {}};
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
      } else if (const auto* created =
                     std::get_if<analysis::ClassCreation>(&instruction.operation)) {
        operation = SourceAssignment{
            analysis::ScalarSlot{analysis::Accumulator{}},
            ClassExpression{*created, SourcePc(instruction)}};
      } else if (const auto* accessor =
                     std::get_if<analysis::DefineAccessor>(&instruction.operation)) {
        if (!SameSlot(effect.normal_registers.write,
                      analysis::ScalarSlot{analysis::Accumulator{}}))
          return Fail(SourceError::kInvalidEffect, instruction.pc);
        // The input accumulator is a naming flag, while the normal result is
        // the object. Preserve both; no accessor descriptor is guessed here.
        operation = SourceAssignment{
            analysis::ScalarSlot{analysis::Accumulator{}},
            AccessorExpression{*accessor, {}}};
      } else if (const auto* store =
                     std::get_if<analysis::ModuleStore>(&instruction.operation)) {
        // Input verification proves the stored accumulator is Language. Slot
        // ownership and initialization belong to the enclosing record, and
        // must still be checked by the printer before publishing source.
        operation = SourceModuleStore{store->slot, analysis::Accumulator{}};
      } else if (const auto* module =
                     std::get_if<analysis::ModuleLoad>(&instruction.operation)) {
        if (module_fact >= proof.module_reads.size() ||
            proof.module_reads[module_fact].instruction.value != position ||
            (method.ssa.IsPhased() &&
             proof.module_reads[module_fact].proof == analysis::ModuleReadProof::kGuarded))
          return Fail(SourceError::kUnsupportedOperation, instruction.pc);
        ++module_fact;
        operation = SourceAssignment{
            analysis::ScalarSlot{analysis::Accumulator{}}, ModuleExpression{*module}};
      } else if (const auto* range =
                     std::get_if<analysis::SuperRange>(&instruction.operation)) {
        if (!derived || derived->super_pc != instruction.pc)
          return Fail(SourceError::kSuperInitialization, instruction.pc);
        operation = SourceAssignment{
            analysis::ScalarSlot{analysis::Accumulator{}}, *range};
      } else if (const auto* check =
                     std::get_if<analysis::SuperCheck>(&instruction.operation)) {
        if (derived) {
          // The whole-constructor proof checks the actual SSA input of every
          // guard. A Language-only local/capture fact cannot replace it.
          operation = analysis::NoOperation{};
        } else {
          switch (check->kind) {
            case analysis::SuperCheckKind::kRequireInitialized: {
              const auto value = ReadValue(method, position,
                  analysis::ScalarSlot{analysis::Accumulator{}});
              const auto kinds = value ? proof.At(*value) : std::nullopt;
              // v7 flag 0 rejects BOTH Hole and undefined. Language-only
              // captures do not prove this check redundant. Defined facts
              // exclude both sentinels, including mixed string/number phis.
              if (!kinds || !kinds->IsDefined()) {
                // A direct captured load may carry the stronger premise to
                // its owner. Do not infer it for all SSA uses or phi inputs:
                // require the unchanged adjacent load in this normal block.
                const auto origin = value ? method.ssa.Definition(*value)
                                          : std::nullopt;
                if (method.ssa.IsPhased() || !origin || position == 0 ||
                    position <= method.flow.blocks[block_id].first_instruction ||
                    origin->value != position - 1)
                  return Fail(SourceError::kSuperInitialization, instruction.pc);
                auto* assignment = std::get_if<SourceAssignment>(
                    &ast.statements.back().operation);
                auto* captured = assignment
                    ? std::get_if<CapturedExpression>(&assignment->value) : nullptr;
                if (!captured || !kinds || !kinds->IsLanguage())
                  return Fail(SourceError::kSuperInitialization, instruction.pc);
                captured->requirement = analysis::CaptureDomain::kDefined;
              }
              operation = analysis::NoOperation{};
              break;
            }
            case analysis::SuperCheckKind::kNoop:
              operation = analysis::NoOperation{};
              break;
            case analysis::SuperCheckKind::kRequireUninitialized:
              return Fail(SourceError::kSuperInitialization, instruction.pc);
          }
        }
      } else if (const auto* guard =
                     std::get_if<analysis::HoleGuard>(&instruction.operation)) {
        const auto value = ReadValue(method, position, guard->value);
        const auto kinds = value ? proof.At(*value) : std::nullopt;
        if (!kinds || !kinds->IsLanguage())
          return Fail(SourceError::kInvalidEffect, instruction.pc);
        // Pinned UNDEFINEDIFHOLE handlers do not even read the error name on
        // the non-Hole path. Unlike SuperCheck, undefined passes this guard.
        // Captured facts remain conditional obligations of the whole template.
        operation = analysis::NoOperation{};
        if (position && module_fact &&
            analysis::IsGuardedModuleRead(proof.module_reads[module_fact - 1].proof) &&
            proof.module_reads[module_fact - 1].instruction.value == position - 1)
          previous_origin = SourcePc(method.instructions[position - 1]);
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
        if (!property_id || !last_effect)
          return Fail(SourceError::kUnsupportedOperation, instruction.pc);
        // Route proof is only half the argument. The preceding SSA use counts
        // include handler phis, preventing removal of observable spill values.
        if (method.ssa.IsPhased() &&
            (i >= proof.call_routes.size() || !proof.call_routes[i] ||
             proof.call_routes[i]->property.value != *property_id))
          return Fail(SourceError::kUnsupportedOperation, instruction.pc);
        const bool across_blocks =
            *property_id < method.flow.blocks[block_id].first_instruction;
        if (method.ssa.IsPhased()) {
          switch (proof.call_routes[i]->region) {
            case analysis::ReceiverRegion::kLinear:
              if (across_blocks) return Fail(SourceError::kUnsupportedOperation, instruction.pc);
              break;
            case analysis::ReceiverRegion::kForward:
              if (!across_blocks || ast.regions.catches)
                return Fail(SourceError::kUnsupportedOperation, instruction.pc);
              break;
          }
        }
        if (across_blocks) {
          if (ast.argument_region)
            return Fail(SourceError::kUnsupportedOperation, instruction.pc);
          ast.argument_region = ProveArgumentRegion(method, *property_id, i);
          if (!ast.argument_region)
            return Fail(SourceError::kUnsupportedOperation, instruction.pc);
        }
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
        const bool staged = across_blocks || *last_effect != property ||
            (method.ssa.IsPhased() && proof.call_routes[i]->evaluation ==
                analysis::ReceiverEvaluation::kPreserveOrder);
        if (method.ssa.IsPhased()) {
          // Effectful argument preparation stays inside the existing staged
          // Reference call. Forward-region admission above requires both the
          // phased caller-exit certificate and the normal single-entry proof;
          // neither permits delayed property evaluation. Linear local-catch
          // routes also require staging for pure arguments, preserving the
          // register snapshot if the getter throws before argument evaluation.
          switch (proof.call_routes[i]->evaluation) {
            case analysis::ReceiverEvaluation::kFuse:
              if (staged) return Fail(SourceError::kUnsupportedOperation, instruction.pc);
              break;
            case analysis::ReceiverEvaluation::kPreserveOrder:
              if (!staged) return Fail(SourceError::kUnsupportedOperation, instruction.pc);
              break;
          }
        }
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
          fused_spill = SourcePc(method.instructions[*spill]);
          fused_reload = SourcePc(method.instructions[*reload]);
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
          fused_read = SourcePc(preceding);
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
        if (ast.regions.order_positions[target->value] <=
            ast.regions.order_positions[block_id])
          operation = SourceConditionalContinue{branch->predicate, *target};
        else
          operation = SourceConditionalBreak{branch->predicate, *target};
      } else if (const auto* jump =
                     std::get_if<analysis::JumpOperation>(
                         &instruction.operation)) {
        const auto target = Target(method.flow, jump->target);
        if (!target) return Fail(SourceError::kMissingTarget, instruction.pc);
        if (ast.regions.order_positions[target->value] <=
            ast.regions.order_positions[block_id])
          operation = SourceContinue{*target};
        else
          operation = SourceBreak{*target};
      } else if (const auto* returned =
                     std::get_if<analysis::ReturnOperation>(
                         &instruction.operation)) {
        operation = SourceReturn{returned->value};
      } else if (const auto* thrown =
                     std::get_if<analysis::ThrowOperation>(&instruction.operation)) {
        if (effect.exceptional != analysis::ExceptionalCompletion::kAlwaysThrow ||
            effect.normal != analysis::LanguageEffect::kNone ||
            effect.before_throw != analysis::LanguageEffect::kNone ||
            effect.normal_registers.write)
          return Fail(SourceError::kInvalidEffect, instruction.pc);
        operation = SourceThrow{thrown->value};
      } else if (std::holds_alternative<analysis::NoOperation>(
                     instruction.operation)) {
        operation = analysis::NoOperation{};
      } else {
        return Fail(SourceError::kUnsupportedOperation, instruction.pc);
      }
      ast.statements.push_back({SourcePc(instruction), std::move(operation), fused_read,
                                fused_spill, fused_reload, read_distance,
                                spill_distance, reload_distance, previous_origin});
      if (!IsPrivateValue(instruction.operation)) last_effect = position;
      copies.Observe(method,
          analysis::InstructionId{static_cast<std::uint32_t>(position)});
      objects.Observe(method,
          analysis::InstructionId{static_cast<std::uint32_t>(position)},
          method.flow.blocks[block_id].first_instruction,
          std::holds_alternative<SourceOwnStore>(ast.statements.back().operation));
    }
    ast.allocated_bytes = ast.regions.allocated_bytes + local_bytes +
                          ast.statements.capacity() * sizeof(SourceStatement) +
                          ast.fallthrough.capacity() *
                              sizeof(std::optional<analysis::BlockId>);
    const auto copy_bytes = remaining - ast.statements.capacity() * sizeof(SourceStatement) -
        ast.fallthrough.capacity() * sizeof(std::optional<analysis::BlockId>);
    if (const auto failure = ElideDeadCopies(method, ast, uses, copy_bytes)) return *failure;
    const auto retained_before = ast.allocated_bytes;
    if (const auto failure = FoldArrayLiterals(method, ast, copy_bytes)) return *failure;
    if (const auto failure = FoldClassAssembly(
            method, ast, copy_bytes - (ast.allocated_bytes - retained_before))) return *failure;
    return ast;
  } catch (const std::bad_alloc&) {
    return Fail(SourceError::kResourceLimit);
  } catch (const std::length_error&) {
    return Fail(SourceError::kResourceLimit);
  }
}

SourceAstResult BuildSourceAst(const analysis::SemanticMethod& method,
    const analysis::SemanticProof& proof, analysis::SemanticRegionTree regions,
    std::size_t budget_bytes, std::span<const std::uint8_t> named_scopes,
    std::optional<analysis::RestPrologue> rest,
    std::optional<analysis::RangeDerivedConstructor> derived) {
  const analysis::InstructionSsa ssa{method.ssa};
  const AstMethod input{method.instructions, method.flow, ssa};
  const AstProof facts{proof.environments.before.size(), proof.values.Facts(),
                       proof.locals, proof.module_reads, proof.captured_reads};
  return BuildSourceAst(input, facts, std::move(regions), budget_bytes,
                        named_scopes, rest, derived);
}
}  // namespace abcd::source
