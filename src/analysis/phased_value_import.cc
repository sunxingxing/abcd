#include "phased_value_import.h"

#include <new>
#include <stdexcept>
#include "phased_value_rules.h"

namespace abcd::analysis {
namespace {
using namespace detail;
using Error = ValueImportError;
void LinkUse(ValueProgram& graph, OperandUse use) {
  const UseId id{static_cast<std::uint32_t>(graph.uses.size())};
  if (const auto* value = std::get_if<ValueId>(&use.operand)) {
    RequirePhase(value->value < graph.values.size(), Error::kInputMismatch);
    auto& definition = graph.values[value->value];
    use.next = definition.first_use;
    if (use.next) graph.uses[use.next->value].previous = id;
    definition.first_use = id;
  }
  graph.uses.push_back(use);
}
}
std::variant<ImportedPhases, ValueImportFailure> BuildPhasedValues(
    const PhasedValueInput& input, SemanticBudget budget) {
  try {
    PhaseQuota quota{budget};
    CheckPhaseInput(input, quota);
    const auto& flow = input.flow;
    const auto values = input.ssa.Values();
    RequirePhase(!flow.instructions.empty(), Error::kInvalidGraph);
    quota.Admit(1, sizeof(ValueProgram));
    ValueProgram graph;
    graph.arguments = input.proof.arguments;
    quota.Admit(1, sizeof(OperandView));
    quota.Reserve(graph.phases, flow.nodes.size());
    quota.Reserve(graph.blocks, flow.nodes.size());
    quota.Reserve(graph.ops, flow.instructions.size());
    quota.Reserve(graph.origins, flow.instructions.size());
    quota.Reserve(graph.input.sites, flow.instructions.size());
    quota.Work(flow.instructions.size());
    graph.input.sites.resize(flow.instructions.size());
    quota.Reserve(graph.values, values.size());
    quota.Reserve(graph.input.values, values.size());
    quota.Reserve(graph.exceptional_exits, flow.instructions.size());
    std::size_t edges = 0;
    std::size_t uses = input.ssa.AllInputs().size();
    RequirePhase(uses <= std::numeric_limits<std::uint32_t>::max(), Error::kResourceLimit);
    for (const auto& node : flow.nodes) {
      quota.Work();
      RequirePhase(node.successor_count <= node.successors.size(), Error::kInvalidGraph);
      edges += node.successor_count;
    }
    for (const auto& instruction : flow.instructions) {
      quota.Work();
      const auto operands = PhaseOperands(instruction, quota);
      RequirePhase(operands.Count() <= std::numeric_limits<std::uint32_t>::max() - uses,
          Error::kResourceLimit);
      uses += operands.Count();
    }
    quota.Reserve(graph.edges, edges);
    quota.Reserve(graph.uses, uses);
    for (std::size_t b = 0; b < flow.nodes.size(); ++b) {
      quota.Work();
      const auto& node = flow.nodes[b];
      RequirePhase(node.instruction.value < flow.instructions.size(), Error::kInputMismatch);
      const auto& instruction = flow.instructions[node.instruction.value];
      const WorkBlockId block{static_cast<std::uint32_t>(b)};
      const OpId op{node.instruction.value};
      graph.blocks.push_back({{static_cast<std::uint32_t>(graph.ops.size())},
          node.phase == ExceptionPhase::kBefore ? 1U : 0U,
          {static_cast<std::uint32_t>(graph.edges.size())}, node.successor_count});
      graph.phases.push_back({{block.value}, {op.value}, node.phase,
          node.phase == ExceptionPhase::kCatch ? std::nullopt : std::optional{op}});
      for (std::uint8_t e = 0; e < node.successor_count; ++e) {
        quota.Work();
        const auto& edge = node.successors[e];
        RequirePhase(edge.target.value < flow.nodes.size(), Error::kInvalidGraph);
        graph.edges.push_back({block, {edge.target.value}, PhaseEdgeKind(edge.kind), e});
      }
      if (node.phase != ExceptionPhase::kBefore) continue;
      RequirePhase(op.value == graph.ops.size(), Error::kInputMismatch, instruction.pc);
      graph.input.sites[op.value] = op;
      // Normalized async retains file provenance independently of working PC.
      // This does not admit raw suspension: operation import still controls it.
      graph.origins.push_back({{op.value}, instruction.pc, instruction.origin});
      graph.ops.push_back({ImportValueOperation(instruction.operation), block, 0, {}, 0, {},
          instruction.effects.normal, instruction.effects.before_throw,
          instruction.effects.exceptional, {op.value}});
      if (node.exception) {
        std::optional<ValueHandler> handler;
        if (node.exception->target) {
          for (const auto& edge : std::span(node.successors).first(node.successor_count)) {
            quota.Work();
            if (edge.kind == ExceptionArcKind::kException) {
              RequirePhase(!handler, Error::kInputMismatch);
              handler = ValueHandler{*node.exception->target, {edge.target.value}};
            }
          }
          RequirePhase(handler.has_value(), Error::kInputMismatch);
        }
        graph.exceptional_exits.push_back({op, {op.value}, instruction.effects.before_throw, handler});
      }
    }
    for (std::size_t v = 0; v < values.size(); ++v) {
      quota.Work();
      const auto& value = values[v];
      ValueDefinition definition{ImportEntryValue(value.slot, input.frame), input.values.Facts()[v], {}};
      if (const auto* result = std::get_if<InstructionValue>(&value.origin)) {
        RequirePhase(result->instruction.value < flow.nodes.size(), Error::kInputMismatch);
        const auto& phase = graph.phases[result->instruction.value];
        const WorkBlockId block{result->instruction.value};
        if (phase.phase == ExceptionPhase::kCatch) {
          definition.origin = CatchDefinition{block};
        } else {
          RequirePhase(phase.phase == ExceptionPhase::kCommit && phase.operation &&
              phase.operation->value < graph.ops.size(), Error::kInputMismatch);
          definition.origin = OpResult{*phase.operation, block};
          auto& op = graph.ops[phase.operation->value];
          RequirePhase(!op.result, Error::kInputMismatch);
          op.result = ValueId{static_cast<std::uint32_t>(v)};
        }
      } else if (const auto* phi = std::get_if<PhiValue>(&value.origin)) {
        definition.origin = PhiDefinition{{phi->block.value}};
      }
      graph.values.push_back(definition);
      graph.input.values.push_back({static_cast<std::uint32_t>(v)});
    }
    const auto reads = input.ssa.AllReads();
    std::size_t cursor = 0;
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      quota.Work();
      auto& op = graph.ops[i];
      const auto& instruction = flow.instructions[i];
      const auto operands = PhaseOperands(instruction, quota);
      op.first_use = {static_cast<std::uint32_t>(graph.uses.size())};
      for (std::uint32_t ordinal = 0; ordinal < operands.Count(); ++ordinal) {
        quota.Work();
        const auto operand = operands.At(ordinal);
        RequirePhase(operand.has_value(), Error::kOperandMismatch);
        if (std::holds_alternative<EmptyArray>(operand->value)) continue;
        ValueOperand value;
        std::optional<InputRead> occurrence;
        if (operand->read_ordinal) {
          RequirePhase(cursor < reads.size(), Error::kInputMismatch);
          const auto& read = reads[cursor++];
          RequirePhase(read.instruction.value == op.block.value && read.ordinal == *operand->read_ordinal,
              Error::kInputMismatch);
          value = ValueId{read.value.value};
          occurrence = InputRead{{static_cast<std::uint32_t>(i)}, read.ordinal};
        } else value = PhaseLiteral(operand->value);
        LinkUse(graph, {OpId{static_cast<std::uint32_t>(i)}, operand->role, operand->purpose,
            value, {}, occurrence, {}, {}});
        ++op.use_count;
      }
    }
    RequirePhase(cursor == reads.size(), Error::kInputMismatch);
    for (const auto& incoming : input.ssa.AllInputs()) {
      quota.Work();
      IncomingKey key = MethodEntryKey{};
      if (const auto* edge = std::get_if<SsaEdge>(&incoming.edge)) {
        RequirePhase(edge->source.value < graph.blocks.size(), Error::kInvalidPhi);
        const auto& source = graph.blocks[edge->source.value];
        RequirePhase(edge->successor < source.edge_count, Error::kInvalidPhi);
        key = EdgeId{source.first_edge.value + edge->successor};
      }
      LinkUse(graph, {ValueId{incoming.phi.value}, OperandRole::kPhiInput, ReadPurpose::kOperand,
          ValueId{incoming.value.value}, key, {}, {}, {}});
    }
    const auto verified = VerifyImportedPhases(graph, input, quota.Remaining());
    if (const auto* error = std::get_if<ValueImportFailure>(&verified)) return *error;
    quota.Work(std::get<PhasedImportProof>(verified).work);
    return ImportedPhases{std::move(graph), budget.bytes - quota.Remaining().bytes,
        budget.work - quota.Remaining().work};
  } catch (const ValueImportFailure& failure) { return failure;
  } catch (const std::bad_alloc&) { return ValueImportFailure{Error::kResourceLimit, {}};
  } catch (const std::length_error&) { return ValueImportFailure{Error::kResourceLimit, {}}; }
}
std::variant<ValueProgram, ValueImportFailure> ImportPhasedValues(
    const PhasedValueInput& input, SemanticBudget budget) {
  auto built = BuildPhasedValues(input, budget);
  if (auto* result = std::get_if<ImportedPhases>(&built)) return std::move(result->graph);
  return std::get<ValueImportFailure>(built);
}
}  // namespace abcd::analysis
