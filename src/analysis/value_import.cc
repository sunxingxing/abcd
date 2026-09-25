#include "abcd/analysis/value_import.h"

#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>

#include "value_operands.h"

namespace abcd::analysis {
namespace {
using Error = ValueImportError;
void Require(bool condition, Error error, std::optional<abc::CodeOffset> pc = {}) {
  if (!condition) throw ValueImportFailure{error, pc};
}
bool SameSlot(const ScalarSlot& a, const ScalarSlot& b) {
  if (a.index() != b.index()) return false;
  const auto* reg = std::get_if<abc::RegisterId>(&a);
  return !reg || *reg == std::get<abc::RegisterId>(b);
}
class ImportBudget {
 public:
  explicit ImportBudget(SemanticBudget budget) : bytes_(budget.bytes), work_(budget.work) {}
  void Work(std::size_t count = 1) {
    Require(count <= work_, Error::kWorkLimit);
    work_ -= count;
  }
  void Admit(std::size_t count, std::size_t width) {
    Require(count <= std::numeric_limits<std::uint32_t>::max() &&
            count <= bytes_ / width, Error::kResourceLimit);
    bytes_ -= count * width;
  }
  template <typename T> void Reserve(std::vector<T>& values, std::size_t count) {
    Admit(count, sizeof(T));
    values.reserve(count);
    if (values.capacity() > count) Admit(values.capacity() - count, sizeof(T));
  }
 private:
  std::size_t bytes_;
  std::uint64_t work_;
};
EntryDefinition Entry(const ScalarSlot& slot, const abc::FrameLayout& frame) {
  const auto* reg = std::get_if<abc::RegisterId>(&slot);
  if (!reg) return UnknownEntry{slot};
  Require(reg->value < frame.registers, Error::kInputMismatch);
  if (frame.this_value == *reg) return ReceiverEntry{};
  if (frame.function == *reg) return CallableEntry{};
  if (frame.new_target == *reg) return NewTargetEntry{};
  if (reg->value >= frame.first_parameter.value &&
      reg->value - frame.first_parameter.value < frame.parameters)
    return ParameterEntry{reg->value - frame.first_parameter.value};
  return UnknownEntry{slot};
}
ValueOperation Operation(const SemanticOperation& operation) {
  return std::visit([](const auto& op) -> ValueOperation {
    using T = std::decay_t<decltype(op)>;
    if constexpr (std::is_same_v<T, ScalarAssignment>) {
      return std::visit([](const auto& expression) -> ValueOperation {
        using E = std::decay_t<decltype(expression)>;
        if constexpr (std::is_same_v<E, ScalarValue>) {
          if (std::holds_alternative<EmptyArray>(expression)) return ValueArrayAllocation{};
          return ValueCopy{};
        } else if constexpr (std::is_same_v<E, UnaryExpression>) {
          return ValueUnary{expression.op};
        } else if constexpr (std::is_same_v<E, BinaryExpression>) {
          return ValueBinary{expression.op};
        } else {
          return ValuePropertyRead{};
        }
      }, op.expression);
    } else if constexpr (std::is_same_v<T, NamedRead>) {
      return ValueNamedRead{op.name};
    } else if constexpr (std::is_same_v<T, NamedWrite>) {
      return ValueNamedWrite{op.name};
    } else if constexpr (std::is_same_v<T, BranchOperation>) {
      return ValueBranch{op.predicate.op};
    } else if constexpr (std::is_same_v<T, JumpOperation>) {
      return ValueJump{};
    } else if constexpr (std::is_same_v<T, ReturnOperation>) {
      return ValueReturn{};
    } else if constexpr (std::is_same_v<T, NoOperation>) {
      return ValueNop{};
    } else if constexpr (std::is_same_v<T, StringLoad>) {
      return ValueString{op.value};
    } else if constexpr (std::is_same_v<T, HoleGuard>) {
      const auto* name = std::get_if<HoleNameSlot>(&op.name);
      return ValueGuard{name ? std::optional{ name->slot } : std::nullopt};
    } else {
      throw ValueImportFailure{Error::kUnsupportedOperation, {}};
    }
  }, operation);
}
void LinkUse(ValueProgram& graph, OperandUse use) {
  const UseId id{static_cast<std::uint32_t>(graph.uses.size())};
  if (const auto* value = std::get_if<ValueId>(&use.operand)) {
    Require(value->value < graph.values.size(), Error::kInputMismatch);
    auto& definition = graph.values[value->value];
    use.next = definition.first_use;
    if (use.next) graph.uses[use.next->value].previous = id;
    definition.first_use = id;
  }
  graph.uses.push_back(std::move(use));
}
}  // namespace

ValueOperation ImportValueOperation(const SemanticOperation& operation) {
  return Operation(operation);
}

std::variant<ValueProgram, ValueImportFailure> ImportNormalValues(
    const abc::MethodBody& body, const SemanticMethod& method,
    const SemanticProof& proof, const abc::FrameLayout& frame,
    SemanticBudget budget) {
  try {
    Require(body.tries.empty() && body.handlers.empty(), Error::kExceptionFlow);
    const auto count = method.instructions.size();
    const auto values = method.ssa.Values();
    Require(count != 0 && count == body.instructions.size() &&
            proof.values.Facts().size() == values.size(), Error::kInputMismatch);
    Require(frame.locals <= frame.registers &&
            frame.first_parameter.value <= frame.registers &&
            frame.parameters <= frame.registers - frame.first_parameter.value,
            Error::kInputMismatch);
    ImportBudget quota{budget};
    ValueProgram graph;
    quota.Reserve(graph.ops, count);
    quota.Reserve(graph.origins, count);
    quota.Reserve(graph.exceptional_exits, count);
    quota.Reserve(graph.input.sites, count);
    graph.input.sites.resize(count);
    quota.Reserve(graph.values, values.size());
    quota.Reserve(graph.input.values, values.size());
    quota.Reserve(graph.blocks, method.flow.blocks.size());
    std::size_t edge_count = 0;
    for (const auto& block : method.flow.blocks) {
      quota.Work();
      Require(block.successor_count <= block.successors.size(), Error::kInvalidGraph);
      edge_count += block.successor_count;
    }
    quota.Reserve(graph.edges, edge_count);
    Require(count <= std::numeric_limits<std::size_t>::max() / kScalarOperandCapacity,
            Error::kResourceLimit);
    const auto operands = count * kScalarOperandCapacity;
    Require(method.ssa.AllInputs().size() <=
            std::numeric_limits<std::size_t>::max() - operands, Error::kResourceLimit);
    const auto use_capacity = operands + method.ssa.AllInputs().size();
    quota.Reserve(graph.uses, use_capacity);
    quota.Admit(use_capacity, sizeof(std::uint8_t));  // VerifyValueUses scratch.
    std::size_t next_instruction = 0;
    for (std::size_t b = 0; b < method.flow.blocks.size(); ++b) {
      quota.Work();
      const auto& block = method.flow.blocks[b];
      Require(block.first_instruction == next_instruction &&
              block.instruction_count <= count - next_instruction, Error::kInvalidGraph);
      graph.blocks.push_back({{block.first_instruction}, block.instruction_count,
          {static_cast<std::uint32_t>(graph.edges.size())}, block.successor_count});
      next_instruction += block.instruction_count;
      for (std::uint8_t e = 0; e < block.successor_count; ++e) {
        quota.Work();
        const auto& edge = block.successors[e];
        Require(edge.kind != FlowEdgeKind::kResume, Error::kResumeFlow);
        Require(edge.target.value < method.flow.blocks.size(), Error::kInvalidGraph);
        graph.edges.push_back({{static_cast<std::uint32_t>(b)}, {edge.target.value},
                               edge.kind, e});
      }
    }
    Require(next_instruction == count, Error::kInvalidGraph);
    for (std::size_t v = 0; v < values.size(); ++v) {
      quota.Work();
      const auto& value = values[v];
      ValueDefinition definition{Entry(value.slot, frame), proof.values.Facts()[v], {}};
      if (const auto* result = std::get_if<InstructionValue>(&value.origin)) {
        Require(result->instruction.value < count, Error::kInputMismatch);
        definition.origin = OpResult{{result->instruction.value}};
      } else if (const auto* phi = std::get_if<PhiValue>(&value.origin)) {
        Require(phi->block.value < graph.blocks.size(), Error::kInvalidPhi);
        definition.origin = PhiDefinition{{phi->block.value}};
      }
      graph.values.push_back(definition);
      graph.input.values.push_back({static_cast<std::uint32_t>(v)});
    }
    // Install result IDs from definitions once; no per-instruction SSA search.
    for (std::size_t b = 0; b < graph.blocks.size(); ++b) {
      const auto& block = graph.blocks[b];
      for (std::uint32_t order = 0; order < block.op_count; ++order) {
        quota.Work();
        const auto index = block.first_op.value + order;
        const auto& instruction = method.instructions[index];
        Require(instruction.pc == body.instructions[index].offset,
                Error::kInputMismatch, instruction.pc);
        Require(!instruction.origin, Error::kResumeFlow, instruction.pc);
        const auto operands = DescribeValueOperands(instruction);
        if (const auto* error = std::get_if<OperandError>(&operands))
          throw ValueImportFailure{
              *error == OperandError::kUnsupportedOperation
                  ? Error::kUnsupportedOperation : Error::kOperandMismatch,
              instruction.pc};
        graph.origins.push_back({{index}, instruction.pc, {}});
        graph.input.sites[index] = OpId{index};
        graph.ops.push_back({Operation(instruction.operation),
            {static_cast<std::uint32_t>(b)}, order, {}, 0, {},
            instruction.effects.normal, instruction.effects.before_throw,
            instruction.effects.exceptional, {index}});
        if (instruction.effects.exceptional != ExceptionalCompletion::kImpossible)
          graph.exceptional_exits.push_back({{index}, {index}, instruction.effects.before_throw});
      }
    }
    for (std::size_t v = 0; v < values.size(); ++v) {
      quota.Work();
      if (const auto* result = std::get_if<OpResult>(&graph.values[v].origin)) {
        auto& op = graph.ops[result->op.value];
        Require(!op.result, Error::kInputMismatch);
        op.result = ValueId{static_cast<std::uint32_t>(v)};
      }
    }
    const auto reads = method.ssa.AllReads();
    std::size_t read_index = 0;
    for (std::size_t i = 0; i < count; ++i) {
      quota.Work();
      const auto& instruction = method.instructions[i];
      const auto described = DescribeValueOperands(instruction);
      const auto* plan = std::get_if<ValueOperands>(&described);
      Require(plan != nullptr, Error::kOperandMismatch, instruction.pc);
      auto& op = graph.ops[i];
      Require(plan->write.has_value() == op.result.has_value(), Error::kInputMismatch, instruction.pc);
      if (op.result)
        Require(SameSlot(*plan->write, values[op.result->value].slot), Error::kInputMismatch, instruction.pc);
      op.first_use = UseId{static_cast<std::uint32_t>(graph.uses.size())};
      for (const auto& operand : std::span(plan->operands).first(plan->count)) {
        quota.Work();
        if (std::holds_alternative<EmptyArray>(operand.value)) continue;
        ValueOperand input;
        std::optional<InputRead> occurrence;
        if (operand.read_ordinal) {
          Require(read_index < reads.size(), Error::kInputMismatch, instruction.pc);
          const auto& read = reads[read_index++];
          Require(read.instruction.value == i && read.ordinal == *operand.read_ordinal &&
                  read.value.value < values.size(), Error::kInputMismatch, instruction.pc);
          const auto slot = std::holds_alternative<Accumulator>(operand.value)
              ? ScalarSlot{Accumulator{}} : ScalarSlot{std::get<abc::RegisterId>(operand.value)};
          Require(SameSlot(slot, values[read.value.value].slot), Error::kInputMismatch, instruction.pc);
          input = ValueId{read.value.value};
          occurrence = InputRead{{static_cast<std::uint32_t>(i)}, read.ordinal};
        } else {
          input = std::visit([](const auto& literal) -> ValueOperand {
            using T = std::decay_t<decltype(literal)>;
            if constexpr (std::is_same_v<T, SpecialValue> ||
                          std::is_same_v<T, IntegerLiteral> || std::is_same_v<T, Float64Bits>)
              return literal;
            else throw ValueImportFailure{Error::kOperandMismatch, {}};
          }, operand.value);
        }
        LinkUse(graph, {OpId{static_cast<std::uint32_t>(i)}, operand.role,
                        operand.purpose, input, {}, occurrence, {}, {}});
        ++op.use_count;
      }
    }
    Require(read_index == reads.size(), Error::kInputMismatch);
    for (const auto& incoming : method.ssa.AllInputs()) {
      quota.Work();
      Require(incoming.phi.value < values.size() && incoming.value.value < values.size(), Error::kInvalidPhi);
      const auto* phi = std::get_if<PhiDefinition>(&graph.values[incoming.phi.value].origin);
      Require(phi != nullptr, Error::kInvalidPhi);
      Require(SameSlot(values[incoming.phi.value].slot,
                       values[incoming.value.value].slot), Error::kInvalidPhi);
      IncomingKey key = MethodEntryKey{};
      if (const auto* edge = std::get_if<SsaEdge>(&incoming.edge)) {
        Require(edge->source.value < graph.blocks.size(), Error::kInvalidPhi);
        const auto& source = graph.blocks[edge->source.value];
        Require(edge->successor < source.edge_count, Error::kInvalidPhi);
        const EdgeId id{source.first_edge.value + edge->successor};
        Require(graph.edges[id.value].target == phi->block, Error::kInvalidPhi);
        key = id;
      } else {
        Require(phi->block.value == 0, Error::kInvalidPhi);
      }
      LinkUse(graph, {ValueId{incoming.phi.value}, OperandRole::kPhiInput,
          ReadPurpose::kOperand, ValueId{incoming.value.value}, key, {}, {}, {}});
    }
    quota.Work(graph.values.size());
    quota.Work(graph.uses.size());
    quota.Work(graph.ops.size());
    Require(!VerifyValueUses(graph, use_capacity), Error::kInvalidGraph);
    return graph;
  } catch (const ValueImportFailure& failure) {
    return failure;
  } catch (const std::bad_alloc&) {
    return ValueImportFailure{Error::kResourceLimit, {}};
  } catch (const std::length_error&) {
    return ValueImportFailure{Error::kResourceLimit, {}};
  }
}
}  // namespace abcd::analysis
