#include "abcd/analysis/value_import.h"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>

#include "value_operands.h"
#include "operand_view.h"

namespace abcd::analysis {
bool MatchesEntryValue(const EntryDefinition& entry, const ScalarSlot& slot,
                  const abc::FrameLayout& frame) {
  const auto* reg = std::get_if<abc::RegisterId>(&slot);
  if (reg) {
    if (reg->value >= frame.registers) return false;
    if (frame.this_value == *reg) return std::holds_alternative<ReceiverEntry>(entry);
    if (frame.function == *reg) return std::holds_alternative<CallableEntry>(entry);
    if (frame.new_target == *reg) return std::holds_alternative<NewTargetEntry>(entry);
    if (reg->value >= frame.first_parameter.value &&
        reg->value - frame.first_parameter.value < frame.parameters) {
      const auto* parameter = std::get_if<ParameterEntry>(&entry);
      return parameter && parameter->index == reg->value - frame.first_parameter.value;
    }
  }
  const auto* unknown = std::get_if<UnknownEntry>(&entry);
  if (!unknown || unknown->slot.index() != slot.index()) return false;
  return !reg || *reg == std::get<abc::RegisterId>(unknown->slot);
}
bool MatchesValueLiteral(const ScalarValue& expected, const ValueOperand& actual) {
  return std::visit([&](const auto& value) {
    using T = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<T, SpecialValue>) {
      const auto* other = std::get_if<T>(&actual);
      return other && value == *other;
    } else if constexpr (std::is_same_v<T, IntegerLiteral> ||
                         std::is_same_v<T, Float64Bits>) {
      const auto* other = std::get_if<T>(&actual);
      return other && value.value == other->value;
    } else return false;
  }, expected);
}
std::size_t ImportedValueScratch(std::size_t uses, std::size_t blocks) {
  constexpr auto kMaxBytes = std::numeric_limits<std::size_t>::max();
  if (blocks > (kMaxBytes - sizeof(OperandView)) / sizeof(WorkBlockId)) return kMaxBytes;
  return std::max(uses, sizeof(OperandView) + blocks * sizeof(WorkBlockId));
}

std::optional<ValueImportFailure> VerifyImportedValues(
    const ValueProgram& graph, const SemanticMethod& method,
    const SemanticProof& proof, const abc::FrameLayout& frame,
    SemanticBudget budget) {
  using Error = ValueImportError;
  const auto fail = [](Error kind, std::optional<abc::CodeOffset> pc = {}) {
    return std::optional<ValueImportFailure>{{kind, pc}};
  };
  if (!graph.phases.empty()) return fail(Error::kExceptionFlow);
  if (budget.bytes < ImportedValueScratch(graph.uses.size(), method.flow.blocks.size())) return fail(Error::kResourceLimit);
  std::uint64_t work = budget.work;
  const auto take = [&](std::size_t count) {
    if (count > work) return false;
    work -= count;
    return true;
  };
  if (!take(graph.values.size()) || !take(graph.ops.size()) ||
      !take(graph.uses.size())) return fail(Error::kWorkLimit);
  if (graph.arguments != proof.arguments ||
      (graph.arguments && (graph.arguments->frame != frame ||
       !std::holds_alternative<ArgumentWindow>(ArgumentWindow::Build(frame)))))
    return fail(Error::kInputMismatch);
  if (const auto invalid = VerifyValueUses(graph, budget.bytes))
    return fail(*invalid == ValueGraphError::kResourceLimit
                    ? Error::kResourceLimit : Error::kInvalidGraph);
  if (graph.revision.value != 0 || graph.cfg_revision.value != 0 ||
      graph.ops.size() != method.instructions.size() ||
      graph.input.sites.size() != graph.ops.size() ||
      graph.origins.size() != graph.ops.size() ||
      graph.values.size() != method.ssa.Values().size() ||
      graph.input.values.size() != graph.values.size() ||
      proof.values.Facts().size() != graph.values.size() ||
      proof.environments.before.size() != graph.ops.size()) return fail(Error::kInputMismatch);
  std::vector<WorkBlockId> block_map;
  try { block_map.resize(method.flow.blocks.size()); }
  catch (const std::bad_alloc&) { return fail(Error::kResourceLimit); }
  catch (const std::length_error&) { return fail(Error::kResourceLimit); }
  if (block_map.capacity() > (budget.bytes - sizeof(OperandView)) / sizeof(WorkBlockId)) return fail(Error::kResourceLimit);
  std::size_t live_blocks = 0, original_site = 0;
  for (std::size_t b = 0; b < method.flow.blocks.size(); ++b) {
    if (!take(1)) return fail(Error::kWorkLimit);
    const auto& input = method.flow.blocks[b];
    if (input.first_instruction != original_site || input.instruction_count == 0 ||
        input.instruction_count > graph.ops.size() - original_site) return fail(Error::kInvalidGraph);
    const bool live = proof.environments.before[original_site].has_value();
    for (std::size_t i = original_site; i < original_site + input.instruction_count; ++i) {
      if (!take(1)) return fail(Error::kWorkLimit);
      if (proof.environments.before[i].has_value() != live) return fail(Error::kInputMismatch);
    }
    original_site += input.instruction_count;
    if (live) block_map[b] = {static_cast<std::uint32_t>(live_blocks++)};
    else {
      if (!live_blocks) return fail(Error::kInvalidGraph);
      block_map[b] = {static_cast<std::uint32_t>(live_blocks - 1)};
    }
  }
  if (original_site != graph.ops.size() || live_blocks != graph.blocks.size()) return fail(Error::kInvalidGraph);
  std::size_t edge_index = 0;
  for (std::size_t b = 0; b < method.flow.blocks.size(); ++b) {
    if (!take(1)) return fail(Error::kWorkLimit);
    const auto& input = method.flow.blocks[b];
    if (!proof.environments.before[input.first_instruction]) continue;
    const auto mapped = block_map[b];
    const auto& block = graph.blocks[mapped.value];
    std::size_t end = input.first_instruction + input.instruction_count;
    for (std::size_t next = b + 1; next < method.flow.blocks.size() &&
        !proof.environments.before[method.flow.blocks[next].first_instruction]; ++next) {
      if (!take(1)) return fail(Error::kWorkLimit);
      end += method.flow.blocks[next].instruction_count;
    }
    if (block.first_op.value != input.first_instruction || block.op_count != end - input.first_instruction ||
        block.first_edge.value != edge_index || block.edge_count != input.successor_count ||
        input.successor_count > input.successors.size()) return fail(Error::kInvalidGraph);
    for (std::uint8_t e = 0; e < input.successor_count; ++e) {
      if (!take(1)) return fail(Error::kWorkLimit);
      if (edge_index >= graph.edges.size()) return fail(Error::kInvalidGraph);
      const auto& edge = graph.edges[edge_index++];
      const auto target = input.successors[e].target.value;
      if (target >= block_map.size() || !proof.environments.before[method.flow.blocks[target].first_instruction] ||
          edge.source != mapped || edge.successor != e ||
          edge.target != block_map[target] || edge.kind != input.successors[e].kind) return fail(Error::kInvalidGraph);
    }
  }
  if (edge_index != graph.edges.size()) return fail(Error::kInvalidGraph);
  for (std::size_t v = 0; v < graph.values.size(); ++v) {
    if (!take(1)) return fail(Error::kWorkLimit);
    if (graph.values[v].life != ValueLife::kLive ||
        graph.input.values[v].value != v ||
        graph.values[v].domain != proof.values.Facts()[v]) return fail(Error::kInputMismatch);
    const auto& input = method.ssa.Values()[v];
    if (const auto* origin = std::get_if<InstructionValue>(&input.origin)) {
      const auto* result = std::get_if<OpResult>(&graph.values[v].origin);
      if (!result || result->op.value != origin->instruction.value) return fail(Error::kInputMismatch);
    } else if (const auto* origin = std::get_if<PhiValue>(&input.origin)) {
      const auto* phi = std::get_if<PhiDefinition>(&graph.values[v].origin);
      if (origin->block.value >= block_map.size() ||
          !proof.environments.before[method.flow.blocks[origin->block.value].first_instruction] ||
          !phi || phi->block != block_map[origin->block.value]) return fail(Error::kInvalidPhi);
    } else {
      const auto* entry = std::get_if<EntryDefinition>(&graph.values[v].origin);
      if (!entry || !MatchesEntryValue(*entry, input.slot, frame)) return fail(Error::kInputMismatch);
    }
  }
  std::size_t read_index = 0;
  std::size_t use_index = 0;
  std::size_t exit_index = 0;
  const auto reads = method.ssa.AllReads();
  bool needs_arguments = false;
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    if (!take(1)) return fail(Error::kWorkLimit);
    const auto& op = graph.ops[i];
    const auto& input = method.instructions[i];
    if (const auto* snapshot = std::get_if<ArgumentSnapshot>(&input.operation)) {
      needs_arguments = true;
      if (!graph.arguments || snapshot->window.First() != frame.first_parameter ||
          snapshot->window.Count() != frame.parameters)
        return fail(Error::kInputMismatch, input.pc);
    }
    const bool reachable = proof.environments.before[i].has_value();
    if (graph.input.sites[i] != OpId{static_cast<std::uint32_t>(i)} ||
        op.origin.value != i || graph.origins[i].site.value != i ||
        graph.origins[i].pc != input.pc || graph.origins[i].original != input.origin ||
        graph.origins[i].disposition != OriginDisposition::kRetained ||
        op.life != (reachable ? ValueLife::kLive : ValueLife::kRemoved) ||
        op.normal != (reachable ? input.effects.normal : LanguageEffect::kNone) ||
        op.before_throw != (reachable ? input.effects.before_throw : LanguageEffect::kNone) ||
        op.exceptional != (reachable ? input.effects.exceptional : ExceptionalCompletion::kImpossible) || op.first_use.value != use_index)
      return fail(Error::kInputMismatch, input.pc);
    if (op.block.value >= graph.blocks.size()) return fail(Error::kInvalidGraph, input.pc);
    const auto& block = graph.blocks[op.block.value];
    if (op.order >= block.op_count || block.first_op.value + op.order != i)
      return fail(Error::kInvalidGraph, input.pc);
    try {
      if (reachable ? op.operation != ImportValueOperation(input.operation) :
          !std::holds_alternative<ValueNop>(op.operation)) return fail(Error::kInputMismatch, input.pc);
    } catch (const ValueImportFailure& error) { return error; }
    if (op.life == ValueLife::kRemoved) {
      if (op.result || op.use_count != 0 || (read_index < reads.size() && reads[read_index].instruction.value == i))
        return fail(Error::kInputMismatch, input.pc);
      continue;
    }
    if (op.exceptional != ExceptionalCompletion::kImpossible) {
      if (exit_index >= graph.exceptional_exits.size()) return fail(Error::kInputMismatch, input.pc);
      const auto& exit = graph.exceptional_exits[exit_index++];
      if (exit.op.value != i || exit.origin != op.origin || exit.before_throw != op.before_throw || exit.handler)
        return fail(Error::kInputMismatch, input.pc);
    }
    const auto described = OperandView::Build(input, {budget.bytes, work});
    const auto* plan = std::get_if<OperandView>(&described);
    if (!plan) {
      const auto error = std::get<OperandViewError>(described);
      return fail(error == OperandViewError::kResourceLimit ? Error::kResourceLimit :
          error == OperandViewError::kWorkLimit ? Error::kWorkLimit : Error::kOperandMismatch, input.pc);
    }
    if (!take(plan->Work())) return fail(Error::kWorkLimit);
    std::size_t actual_count = 0;
    for (std::uint32_t ordinal = 0; ordinal < plan->Count(); ++ordinal) {
      if (!take(1)) return fail(Error::kWorkLimit);
      const auto operand = plan->At(ordinal);
      if (!operand) return fail(Error::kOperandMismatch, input.pc);
      const auto& expected = *operand;
      if (std::holds_alternative<EmptyArray>(expected.value)) continue;
      ++actual_count;
      if (use_index >= graph.uses.size()) return fail(Error::kOperandMismatch, input.pc);
      const auto& use = graph.uses[use_index++];
      if (use.role != expected.role || use.purpose != expected.purpose)
        return fail(Error::kOperandMismatch, input.pc);
      if (expected.read_ordinal) {
        if (read_index >= reads.size() || !use.input) return fail(Error::kOperandMismatch, input.pc);
        const auto& read = reads[read_index++];
        const auto* value = std::get_if<ValueId>(&use.operand);
        if (!value || value->value != read.value.value || read.instruction.value != i ||
            read.ordinal != *expected.read_ordinal || use.input->site.value != i ||
            use.input->ordinal != read.ordinal) return fail(Error::kOperandMismatch, input.pc);
      } else if (use.input || !MatchesValueLiteral(expected.value, use.operand)) {
        return fail(Error::kOperandMismatch, input.pc);
      }
    }
    if (actual_count != op.use_count) return fail(Error::kOperandMismatch, input.pc);
  }
  if (needs_arguments != graph.arguments.has_value() ||
      exit_index != graph.exceptional_exits.size() || read_index != reads.size())
    return fail(Error::kInputMismatch);
  for (const auto& incoming : method.ssa.AllInputs()) {
    if (!take(1)) return fail(Error::kWorkLimit);
    if (use_index >= graph.uses.size()) return fail(Error::kInvalidPhi);
    const auto& use = graph.uses[use_index++];
    const auto* owner = std::get_if<ValueId>(&use.owner);
    const auto* value = std::get_if<ValueId>(&use.operand);
    if (!owner || !value || !use.incoming || owner->value != incoming.phi.value ||
        value->value != incoming.value.value) return fail(Error::kInvalidPhi);
    if (const auto* edge = std::get_if<SsaEdge>(&incoming.edge)) {
      const auto* key = std::get_if<EdgeId>(&*use.incoming);
      if (!key || key->value >= graph.edges.size()) return fail(Error::kInvalidPhi);
      const auto& actual = graph.edges[key->value];
      if (edge->source.value >= block_map.size() ||
          !proof.environments.before[method.flow.blocks[edge->source.value].first_instruction] ||
          actual.source != block_map[edge->source.value] || actual.successor != edge->successor)
        return fail(Error::kInvalidPhi);
    } else if (!std::holds_alternative<MethodEntryKey>(*use.incoming)) return fail(Error::kInvalidPhi);
  }
  if (use_index != graph.uses.size()) return fail(Error::kInputMismatch);
  return {};
}
}  // namespace abcd::analysis
