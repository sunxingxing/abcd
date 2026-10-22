#include "phased_value_import.h"

#include <new>
#include <stdexcept>
#include "phased_value_rules.h"
#include "value_flow.h"

namespace abcd::analysis {
namespace {
using namespace detail;
using Error = ValueImportError;
bool SameThrownValue(const ScalarValue& expected, const ScalarValue& actual) {
  if (const auto* reg = std::get_if<abc::RegisterId>(&expected)) {
    const auto* other = std::get_if<abc::RegisterId>(&actual);
    return other && *reg == *other;
  }
  if (std::holds_alternative<Accumulator>(expected))
    return std::holds_alternative<Accumulator>(actual);
  return MatchesValueLiteral(expected, PhaseLiteral(actual));
}
void CheckAvailability(const ValueProgram& graph, PhaseQuota& quota) {
  const auto checked = VerifyPhasedValueFlow(graph, quota.Remaining());
  if (const auto* proof = std::get_if<PhasedValueFlowProof>(&checked)) {
    quota.Work(proof->work);
    quota.Admit(proof->allocated_bytes, 1);
    return;
  }
  auto error = Error::kInvalidGraph;
  switch (std::get<ValueAvailabilityError>(checked)) {
    case ValueAvailabilityError::kWorkLimit: error = Error::kWorkLimit; break;
    case ValueAvailabilityError::kResourceLimit: error = Error::kResourceLimit; break;
    case ValueAvailabilityError::kUnreachableBlock: error = Error::kUnreachableFlow; break;
    case ValueAvailabilityError::kResumeFlow: error = Error::kResumeFlow; break;
    case ValueAvailabilityError::kInvalidPhi: error = Error::kInvalidPhi; break;
    case ValueAvailabilityError::kInvalidGraph:
    case ValueAvailabilityError::kInvalidTerminator:
    case ValueAvailabilityError::kUnavailableValue: break;
  }
  throw ValueImportFailure{error, {}};
}
void CheckPhases(const ValueProgram& graph, const ExceptionFlow& flow,
                 std::span<const PhaseReachability> reachable, PhaseQuota& quota) {
  std::size_t exits = 0;
  std::size_t operations = 0;
  for (std::size_t b = 0; b < flow.nodes.size(); ++b) {
    quota.Work();
    const auto& node = flow.nodes[b];
    const auto& phase = graph.phases[b];
    const auto& block = graph.blocks[b];
    const bool live = reachable[b] == PhaseReachability::kReachable;
    RequirePhase(phase.input.value == b && phase.instruction.value == node.instruction.value &&
        phase.phase == node.phase && phase.reachability == reachable[b] &&
        block.edge_count == (live ? node.successor_count : 0) &&
        node.successor_count <= node.successors.size(), Error::kInputMismatch);
    std::optional<WorkBlockId> caught;
    for (std::uint8_t e = 0; live && e < node.successor_count; ++e) {
      quota.Work();
      const auto& arc = node.successors[e];
      const auto& edge = graph.edges[block.first_edge.value + e];
      RequirePhase(edge.target.value == arc.target.value && edge.kind == PhaseEdgeKind(arc.kind),
          Error::kInputMismatch);
      if (arc.kind == ExceptionArcKind::kException) {
        RequirePhase(!caught, Error::kInputMismatch);
        caught = edge.target;
      }
    }
    if (node.phase != ExceptionPhase::kBefore) {
      RequirePhase(!node.exception && !caught, Error::kInputMismatch);
      continue;
    }
    RequirePhase(phase.operation && phase.operation->value == operations &&
        node.instruction.value == operations, Error::kInputMismatch);
    const auto& instruction = flow.instructions[operations];
    const auto& op = graph.ops[operations];
    if (!live) { ++operations; continue; }
    RequirePhase(node.exception.has_value() ==
        (instruction.effects.exceptional != ExceptionalCompletion::kImpossible), Error::kInputMismatch);
    if (node.exception) {
      const auto& exception = *node.exception;
      RequirePhase(exits < graph.exceptional_exits.size() && exception.before == instruction.pc &&
          exception.completion == instruction.effects.exceptional, Error::kInputMismatch);
      if (const auto* thrown = std::get_if<ThrowOperation>(&instruction.operation);thrown&&thrown->kind==ThrowKind::kValue) {
        const auto* payload = std::get_if<ThrownValue>(&exception.exception);
        RequirePhase(payload && SameThrownValue(thrown->value, payload->value), Error::kInputMismatch);
      } else RequirePhase(std::holds_alternative<PendingException>(exception.exception), Error::kInputMismatch);
      const auto& exit = graph.exceptional_exits[exits++];
      RequirePhase(exit.op.value == operations && exit.origin == op.origin &&
          exit.before_throw == op.before_throw &&
          exit.handler.has_value() == exception.target.has_value() &&
          caught.has_value() == exception.target.has_value(), Error::kInputMismatch);
      if (exit.handler) {
        RequirePhase(exit.handler->dispatch == *exception.target && exit.handler->entry == *caught,
            Error::kInputMismatch);
        const auto handler = graph.phases[caught->value].instruction;
        RequirePhase(handler.value < flow.instructions.size() &&
            flow.instructions[handler.value].pc == exception.target->handler_pc, Error::kInputMismatch);
      }
    } else RequirePhase(!caught, Error::kInputMismatch);
    ++operations;
  }
  RequirePhase(exits == graph.exceptional_exits.size() && operations == graph.ops.size(), Error::kInputMismatch);
}
void CheckDefinitions(const ValueProgram& graph, const PhasedValueInput& input, PhaseQuota& quota) {
  const auto values = input.ssa.Values();
  for (std::size_t v = 0; v < values.size(); ++v) {
    quota.Work();
    const auto& source = values[v];
    const auto& target = graph.values[v];
    RequirePhase(graph.input.values[v].value == v && target.domain == input.values.Facts()[v] &&
        target.life == ValueLife::kLive, Error::kInputMismatch);
    if (const auto* result = std::get_if<InstructionValue>(&source.origin)) {
      RequirePhase(result->instruction.value < input.flow.nodes.size(), Error::kInputMismatch);
      const auto& phase = graph.phases[result->instruction.value];
      RequirePhase(phase.reachability == PhaseReachability::kReachable, Error::kInputMismatch);
      if (phase.phase == ExceptionPhase::kCatch) {
        const auto* caught = std::get_if<CatchDefinition>(&target.origin);
        RequirePhase(caught && caught->block.value == result->instruction.value &&
            std::holds_alternative<Accumulator>(source.slot), Error::kInputMismatch);
      } else {
        const auto* produced = std::get_if<OpResult>(&target.origin);
        RequirePhase(phase.phase == ExceptionPhase::kCommit && produced && phase.operation == produced->op &&
            produced->commit == WorkBlockId{result->instruction.value}, Error::kInputMismatch);
        const auto& effect = input.flow.instructions[phase.instruction.value].effects.normal_registers;
        RequirePhase(effect.write && SameSlot(*effect.write, source.slot), Error::kInputMismatch);
      }
    } else if (const auto* phi = std::get_if<PhiValue>(&source.origin)) {
      const auto* joined = std::get_if<PhiDefinition>(&target.origin);
      RequirePhase(joined && joined->block.value == phi->block.value &&
          joined->block.value < graph.phases.size() &&
          graph.phases[joined->block.value].reachability == PhaseReachability::kReachable, Error::kInputMismatch);
    } else {
      const auto* entry = std::get_if<EntryDefinition>(&target.origin);
      RequirePhase(entry && MatchesEntryValue(*entry, source.slot, input.frame), Error::kInputMismatch);
    }
  }
}
void CheckOperands(const ValueProgram& graph, const PhasedValueInput& input, PhaseQuota& quota) {
  const auto reads = input.ssa.AllReads();
  const auto values = input.ssa.Values();
  std::size_t cursor = 0;
  std::size_t uses = 0;
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    quota.Work();
    const auto& op = graph.ops[i];
    const auto& instruction = input.flow.instructions[i];
    const auto& origin = graph.origins[i];
    RequirePhase(graph.input.sites[i] == OpId{static_cast<std::uint32_t>(i)} &&
        origin.site.value == i && origin.pc == instruction.pc && origin.original == instruction.origin &&
        origin.disposition == OriginDisposition::kRetained && op.origin.value == i &&
        op.first_use.value == uses, Error::kInputMismatch, instruction.pc);
    if (graph.phases[op.block.value].reachability == PhaseReachability::kUnreachable) {
      RequirePhase(op.life == ValueLife::kRemoved && std::holds_alternative<ValueNop>(op.operation) &&
          !op.result && op.use_count == 0 && op.normal == LanguageEffect::kNone &&
          op.before_throw == LanguageEffect::kNone && op.exceptional == ExceptionalCompletion::kImpossible &&
          op.exception_cause == ExceptionCause::kUnclassified, Error::kInputMismatch, instruction.pc);
      continue;
    }
    RequirePhase(op.life == ValueLife::kLive && op.operation == ImportValueOperation(instruction.operation) &&
        op.normal == instruction.effects.normal && op.before_throw == instruction.effects.before_throw &&
        op.exceptional == instruction.effects.exceptional && op.exception_cause == instruction.effects.exception_cause && op.first_use.value == uses,
        Error::kInputMismatch, instruction.pc);
    const auto operands = PhaseOperands(instruction, quota);
    RequirePhase(operands.Write().has_value() == op.result.has_value(), Error::kInputMismatch);
    if (op.result) RequirePhase(SameSlot(*operands.Write(), values[op.result->value].slot), Error::kInputMismatch);
    std::size_t count = 0;
    for (std::uint32_t ordinal = 0; ordinal < operands.Count(); ++ordinal) {
      quota.Work();
      const auto expected = operands.At(ordinal);
      RequirePhase(expected.has_value(), Error::kOperandMismatch);
      if (std::holds_alternative<EmptyArray>(expected->value)) continue;
      RequirePhase(uses < graph.uses.size(), Error::kOperandMismatch);
      const auto& use = graph.uses[uses++];
      RequirePhase(std::get_if<OpId>(&use.owner) && std::get<OpId>(use.owner).value == i &&
          use.role == expected->role && use.purpose == expected->purpose && !use.incoming,
          Error::kOperandMismatch);
      if (expected->read_ordinal) {
        RequirePhase(cursor < reads.size(), Error::kInputMismatch);
        const auto& read = reads[cursor++];
        const auto* actual = std::get_if<ValueId>(&use.operand);
        RequirePhase(read.instruction.value == op.block.value && read.ordinal == *expected->read_ordinal &&
            actual && actual->value == read.value.value && use.input && use.input->site.value == i &&
            use.input->ordinal == read.ordinal, Error::kInputMismatch);
        const auto* reg = std::get_if<abc::RegisterId>(&expected->value);
        RequirePhase(reg || std::holds_alternative<Accumulator>(expected->value), Error::kOperandMismatch);
        const ScalarSlot slot = reg ? ScalarSlot{*reg} : ScalarSlot{Accumulator{}};
        RequirePhase(SameSlot(slot, values[read.value.value].slot), Error::kInputMismatch);
      } else RequirePhase(!use.input && MatchesValueLiteral(expected->value, use.operand), Error::kOperandMismatch);
      ++count;
    }
    RequirePhase(count == op.use_count, Error::kOperandMismatch);
  }
  RequirePhase(cursor == reads.size(), Error::kInputMismatch);
  for (const auto& incoming : input.ssa.AllInputs()) {
    quota.Work();
    RequirePhase(uses < graph.uses.size() && incoming.phi.value < values.size() &&
        incoming.value.value < values.size(), Error::kInvalidPhi);
    const auto& use = graph.uses[uses++];
    const auto* owner = std::get_if<ValueId>(&use.owner);
    const auto* value = std::get_if<ValueId>(&use.operand);
    RequirePhase(owner && owner->value == incoming.phi.value && value && value->value == incoming.value.value &&
        use.incoming && !use.input && use.role == OperandRole::kPhiInput &&
        use.purpose == ReadPurpose::kOperand &&
        SameSlot(values[incoming.phi.value].slot, values[incoming.value.value].slot), Error::kInvalidPhi);
    if (const auto* edge = std::get_if<SsaEdge>(&incoming.edge)) {
      RequirePhase(edge->source.value < graph.blocks.size(), Error::kInvalidPhi);
      const auto& block = graph.blocks[edge->source.value];
      const auto* actual = std::get_if<EdgeId>(&*use.incoming);
      RequirePhase(edge->successor < block.edge_count && actual &&
          actual->value == block.first_edge.value + edge->successor, Error::kInvalidPhi);
    } else RequirePhase(std::holds_alternative<MethodEntryKey>(*use.incoming), Error::kInvalidPhi);
  }
  RequirePhase(uses == graph.uses.size(), Error::kInputMismatch);
}
}  // namespace
std::variant<PhasedImportProof, ValueImportFailure> VerifyImportedPhases(
    const ValueProgram& graph, const PhasedValueInput& input, SemanticBudget budget) {
  try {
    PhaseQuota quota{budget};
    const auto reachable = CheckPhaseInput(input, quota);
    RequirePhase(graph.revision.value == 0 && graph.cfg_revision.value == 0 &&
        graph.phases.size() == input.flow.nodes.size() && graph.blocks.size() == graph.phases.size() &&
        graph.ops.size() == input.flow.instructions.size() && graph.origins.size() == graph.ops.size() &&
        graph.input.sites.size() == graph.ops.size() && graph.values.size() == input.ssa.Values().size() &&
        graph.input.values.size() == graph.values.size() && graph.arguments == input.proof.arguments,
        Error::kInputMismatch);
    quota.Admit(1, sizeof(OperandView));
    CheckAvailability(graph, quota);
    CheckPhases(graph, input.flow, reachable, quota);
    CheckDefinitions(graph, input, quota);
    CheckOperands(graph, input, quota);
    return PhasedImportProof{budget.work - quota.Remaining().work, budget.bytes - quota.Remaining().bytes};
  } catch (const ValueImportFailure& failure) { return failure;
  } catch (const std::bad_alloc&) { return ValueImportFailure{Error::kResourceLimit, {}};
  } catch (const std::length_error&) { return ValueImportFailure{Error::kResourceLimit, {}}; }
}
}  // namespace abcd::analysis
