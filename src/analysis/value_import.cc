#include "abcd/analysis/value_import.h"

#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>

#include "value_operands.h"
#include "operand_view.h"
#include "value_flow.h"
#include "argument_inputs.h"

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
  SemanticBudget Remaining() const { return {bytes_, work_}; }
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
    } else if constexpr (std::is_same_v<T, AcquireIterator>) {
      return ValueIteratorAcquire{};
    } else if constexpr (std::is_same_v<T, ObjectCheck>) {
      return ValueObjectCheck{};
    } else if constexpr (std::is_same_v<T, PropertyIterator>) {
      return ValueEnumeration{EnumerationAction::kAcquire};
    } else if constexpr (std::is_same_v<T, NextPropertyName>) {
      return ValueEnumeration{EnumerationAction::kNext};
    } else if constexpr (std::is_same_v<T, PropertyDelete>) {
      return ValuePropertyDelete{};
    } else if constexpr (std::is_same_v<T, NamedRead>) {
      return ValueNamedRead{op.name, op.lookup};
    } else if constexpr (std::is_same_v<T, EmptyObjectCreation>) {
      return ValueObjectAllocation{};
    } else if constexpr (std::is_same_v<T, IterResult>) {
      return ValueIterResult{};
    } else if constexpr (std::is_same_v<T, ModuleLoad>) {
      return ValueModuleRead{op.space, op.slot, op.loading};
    } else if constexpr (std::is_same_v<T, ModuleStore>) {
      return ValueModuleWrite{op.slot};
    } else if constexpr (std::is_same_v<T, BufferedArrayCreation>) {
      return ValueArrayAllocation{op.literal};
    } else if constexpr (std::is_same_v<T, BufferedObjectCreation>) {
      return ValueObjectAllocation{op.literal};
    } else if constexpr (std::is_same_v<T, ClassCreation>) {
      return ValueClassCreation{op.constructor, op.literal, op.length, op.cache_slot};
    } else if constexpr (std::is_same_v<T, FunctionCreation>) {
      if ((op.kind != CreationKind::kFunction || op.home_object != HomeObjectInput::kCurrentFunction) &&
          (op.kind != CreationKind::kMethod || op.home_object != HomeObjectInput::kAccumulator))
        throw ValueImportFailure{Error::kUnsupportedOperation, {}};
      return ValueFunctionCreation{op.kind, op.method, op.length.value, op.home_object, op.cache_slot};
    } else if constexpr (std::is_same_v<T, LexicalLoad>) {
      return ValueLexicalLoad{op.address};
    } else if constexpr (std::is_same_v<T, LexicalStore>) {
      return ValueLexicalStore{op.address};
    } else if constexpr (std::is_same_v<T, PushEnvironment>) {
      return ValueEnvironmentPush{op.slots, op.scope};
    } else if constexpr (std::is_same_v<T, PopEnvironment>) {
      return ValueEnvironmentPop{};
    } else if constexpr (std::is_same_v<T, CurrentEnvironment>) {
      return ValueCurrentEnvironment{};
    } else if constexpr (std::is_same_v<T, NamedWrite>) {
      return ValueNamedWrite{op.name};
    } else if constexpr (std::is_same_v<T, ValueWrite>) {
      return ValuePropertyWrite{};
    } else if constexpr (std::is_same_v<T, DefineAccessor>) {
      return ValueDefineAccessor{};
    } else if constexpr (std::is_same_v<T, ComputedOwnWrite>) {
      return ValueComputedOwnWrite{};
    } else if constexpr (std::is_same_v<T, ArraySpread>) {
      return ValueArraySpread{};
    } else if constexpr (std::is_same_v<T, IndexedWrite>) {
      return ValueIndexedWrite{op.index, op.cache_slot, op.kind};
    } else if constexpr (std::is_same_v<T, OwnWrite>) {
      switch (op.kind) {
        case OwnWriteKind::kStore: case OwnWriteKind::kDefineField:
          return ValueOwnWrite{op.kind, op.name, op.cache_slot};
      }
      throw ValueImportFailure{Error::kUnsupportedOperation, {}};
    } else if constexpr (std::is_same_v<T, BranchOperation>) {
      return ValueBranch{op.predicate.op};
    } else if constexpr (std::is_same_v<T, JumpOperation>) {
      return ValueJump{};
    } else if constexpr (std::is_same_v<T, ReturnOperation>) {
      return ValueReturn{};
    } else if constexpr (std::is_same_v<T, ThrowOperation>) {
      return ValueThrow{};
    } else if constexpr (std::is_same_v<T, ConcurrentNotification>) {
      return ValueConcurrentNotification{op.owner};
    } else if constexpr (std::is_same_v<T, NoOperation>) {
      return ValueNop{};
    } else if constexpr (std::is_same_v<T, DebuggerOperation>) {
      return ValueDebugger{};
    } else if constexpr (std::is_same_v<T, FrameThisLoad>) {
      return ValueFrameThis{};
    } else if constexpr (std::is_same_v<T, FrameFunctionLoad>) {
      return ValueFrameFunction{};
    } else if constexpr (std::is_same_v<T, GlobalObjectLoad>) {
      return ValueGlobalObject{};
    } else if constexpr (std::is_same_v<T, SymbolConstructorLoad>) {
      return ValueSymbolConstructor{};
    } else if constexpr (std::is_same_v<T, RestCopy>) {
      return ValueRestArguments{op.first_argument};
    } else if constexpr (std::is_same_v<T, ArgumentSnapshot>) {
      return ValueArgumentSnapshot{op.window.Count()};
    } else if constexpr (std::is_same_v<T, StringLoad>) {
      return ValueString{op.value};
    } else if constexpr (std::is_same_v<T, GlobalAccess>) {
      switch (op.operation) {
        case GlobalOp::kReadName: return ValueGlobalRead{GlobalLookup::kName, op.name};
        case GlobalOp::kReadVar: return ValueGlobalRead{GlobalLookup::kVar, op.name};
        case GlobalOp::kWriteName: case GlobalOp::kWriteVar:
        case GlobalOp::kInitConst: case GlobalOp::kInitMutable:
          throw ValueImportFailure{Error::kUnsupportedOperation, {}};
      }
      throw ValueImportFailure{Error::kUnsupportedOperation, {}};
    } else if constexpr (std::is_same_v<T, LoadHole>) {
      return ValueHole{};
    } else if constexpr (std::is_same_v<T, AwaitValue>) {
      return ValueAwait{op.origin};
    } else if constexpr (std::is_same_v<T, YieldValue>) {
      return ValueYield{op.origin};
    } else if constexpr (std::is_same_v<T, HoleGuard>) {
      const auto* name = std::get_if<HoleNameSlot>(&op.name);
      return ValueGuard{name ? std::optional{ name->slot } : std::nullopt};
    } else if constexpr (std::is_same_v<T, OrdinaryCall>) {
      return ValueInvoke{InvokeReceiver::kUndefined, op.Count()};
    } else if constexpr (std::is_same_v<T, ReceiverCall>) {
      return ValueInvoke{InvokeReceiver::kExplicit, op.Count()};
    } else if constexpr (std::is_same_v<T, SuperForward>) {
      return ValueSuperCall{0, {}, SuperArgumentSource::kInvocationFrame};
    } else if constexpr (std::is_same_v<T, SuperRange>) {
      return ValueSuperCall{op.count, op.cache_slot};
    } else if constexpr (std::is_same_v<T, SuperSpread>) {
      return ValueSuperSpread{op.cache_slot};
    } else if constexpr (std::is_same_v<T, SuperCheck>) {
      return ValueSuperCheck{op.kind};
    } else if constexpr (std::is_same_v<T, ConstructorCall>) {
      return ValueConstruct{op.ArgumentCount()};
    } else if constexpr (std::is_same_v<T, ApplyConstructorCall>) {
      return ValueConstructApply{op.profile_slot};
    } else {
      throw ValueImportFailure{Error::kUnsupportedOperation, {}};
    }
  }, operation);
}
OperandView Operands(const SemanticInstruction& instruction, ImportBudget& quota) {
  auto result = OperandView::Build(instruction, quota.Remaining());
  if (auto* view = std::get_if<OperandView>(&result)) {
    quota.Work(view->Work());
    return std::move(*view);
  }
  Error error = Error::kOperandMismatch;
  switch (std::get<OperandViewError>(result)) {
    case OperandViewError::kUnsupported: error = Error::kUnsupportedOperation; break;
    case OperandViewError::kMismatch: break;
    case OperandViewError::kResourceLimit: error = Error::kResourceLimit; break;
    case OperandViewError::kWorkLimit: error = Error::kWorkLimit; break;
  }
  throw ValueImportFailure{error, instruction.pc};
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
EntryDefinition ImportEntryValue(const ScalarSlot& slot, const abc::FrameLayout& frame) {
  return Entry(slot, frame);
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
    const auto checked = VerifyArgumentInputs(body, method, frame.registers,
        frame.locals, proof.arguments ? &*proof.arguments : nullptr, proof.environments.before, quota.Remaining());
    if (const auto* failure = std::get_if<SemanticProofFailure>(&checked))
      throw ValueImportFailure{failure->kind == SemanticProofError::kResourceLimit
          ? Error::kWorkLimit : Error::kInputMismatch, failure->pc};
    const auto& argument_inputs = std::get<ArgumentInputCheck>(checked);
    quota.Work(argument_inputs.work);
    Require(argument_inputs.fact == proof.arguments, Error::kInputMismatch);
    ValueProgram graph;
    graph.arguments = argument_inputs.fact;
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
    // Scalar literals need bounded slots; variable calls need one slot per SSA
    // read occurrence. Do not cap a range at the fixed scalar operand width.
    const auto scalar_operands = count * kScalarOperandCapacity;
    Require(method.ssa.AllReads().size() <= std::numeric_limits<std::size_t>::max() - scalar_operands,
            Error::kResourceLimit);
    const auto operands = scalar_operands + method.ssa.AllReads().size();
    Require(method.ssa.AllInputs().size() <=
            std::numeric_limits<std::size_t>::max() - operands, Error::kResourceLimit);
    const auto use_capacity = operands + method.ssa.AllInputs().size();
    quota.Reserve(graph.uses, use_capacity);
    quota.Admit(use_capacity, sizeof(std::uint8_t));  // VerifyValueUses scratch.
    // Preserve physical instruction/OpId identity, including dead arena slots.
    // Only executable blocks/edges enter the work CFG. Dead storage following
    // a live block belongs to that block as removed slots, never as effects or
    // fabricated SSA results; all origin PCs and source occurrence IDs survive.
    std::vector<WorkBlockId> block_map;
    quota.Reserve(block_map, method.flow.blocks.size());
    block_map.resize(method.flow.blocks.size());
    Require(proof.environments.before.size() == count, Error::kInputMismatch);
    std::size_t next_instruction = 0;
    for (std::size_t b = 0; b < method.flow.blocks.size(); ++b) {
      quota.Work();
      const auto& block = method.flow.blocks[b];
      Require(block.first_instruction == next_instruction && block.instruction_count != 0 &&
              block.instruction_count <= count - next_instruction, Error::kInvalidGraph);
      const bool reachable = proof.environments.before[block.first_instruction].has_value();
      for (std::size_t i = next_instruction; i < next_instruction + block.instruction_count; ++i) {
        quota.Work(); Require(proof.environments.before[i].has_value() == reachable, Error::kInputMismatch);
      }
      next_instruction += block.instruction_count;
      if (reachable) {
        const WorkBlockId id{static_cast<std::uint32_t>(graph.blocks.size())};
        block_map[b] = id;
        graph.blocks.push_back({{block.first_instruction}, block.instruction_count,
            {static_cast<std::uint32_t>(graph.edges.size())}, block.successor_count});
        for (std::uint8_t e = 0; e < block.successor_count; ++e) {
          quota.Work();
          const auto& edge = block.successors[e];
          Require(edge.kind != FlowEdgeKind::kResume, Error::kResumeFlow);
          Require(edge.target.value < method.flow.blocks.size(), Error::kInvalidGraph);
          const auto& target = method.flow.blocks[edge.target.value];
          Require(target.first_instruction < count && proof.environments.before[target.first_instruction].has_value(), Error::kInvalidGraph);
          // Targets remain original until the complete block map exists.
          graph.edges.push_back({id, {edge.target.value}, edge.kind, e});
        }
      } else {
        Require(!graph.blocks.empty(), Error::kInvalidGraph);
        block_map[b] = {static_cast<std::uint32_t>(graph.blocks.size() - 1)};
        graph.blocks.back().op_count += block.instruction_count;
      }
    }
    Require(next_instruction == count, Error::kInvalidGraph);
    for (auto& edge : graph.edges) { quota.Work(); edge.target = block_map[edge.target.value]; }
    for (std::size_t v = 0; v < values.size(); ++v) {
      quota.Work();
      const auto& value = values[v];
      ValueDefinition definition{Entry(value.slot, frame), proof.values.Facts()[v], {}};
      if (const auto* result = std::get_if<InstructionValue>(&value.origin)) {
        Require(result->instruction.value < count, Error::kInputMismatch);
        definition.origin = OpResult{{result->instruction.value}};
      } else if (const auto* phi = std::get_if<PhiValue>(&value.origin)) {
        Require(phi->block.value < block_map.size() &&
            proof.environments.before[method.flow.blocks[phi->block.value].first_instruction].has_value(), Error::kInvalidPhi);
        definition.origin = PhiDefinition{block_map[phi->block.value]};
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
        const auto operands = Operands(instruction, quota);
        graph.origins.push_back({{index}, instruction.pc, {}});
        graph.input.sites[index] = OpId{index};
        const bool reachable = proof.environments.before[index].has_value();
        // Dead input storage carries provenance, not executable effects. Keep
        // the original operation in the immutable semantic input; a work NOP
        // permits environment analysis without calling an effectful pop pure.
        graph.ops.push_back({reachable ? Operation(instruction.operation) : ValueOperation{ValueNop{}},
            {static_cast<std::uint32_t>(b)}, order, {}, 0, {},
            reachable ? instruction.effects.normal : LanguageEffect::kNone,
            reachable ? instruction.effects.before_throw : LanguageEffect::kNone,
            reachable ? instruction.effects.exceptional : ExceptionalCompletion::kImpossible, {index},
            reachable ? ValueLife::kLive : ValueLife::kRemoved,
            reachable ? instruction.effects.exception_cause : ExceptionCause::kUnclassified});
        if (proof.environments.before[index] && instruction.effects.exceptional != ExceptionalCompletion::kImpossible)
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
      auto& op = graph.ops[i];
      op.first_use = UseId{static_cast<std::uint32_t>(graph.uses.size())};
      if (op.life == ValueLife::kRemoved) {
        Require(!op.result && (read_index == reads.size() || reads[read_index].instruction.value != i), Error::kInputMismatch, instruction.pc);
        continue;
      }
      const auto plan = Operands(instruction, quota);
      Require(plan.Write().has_value() == op.result.has_value(), Error::kInputMismatch, instruction.pc);
      if (op.result)
        Require(SameSlot(*plan.Write(), values[op.result->value].slot), Error::kInputMismatch, instruction.pc);
      op.first_use = UseId{static_cast<std::uint32_t>(graph.uses.size())};
      for (std::uint32_t ordinal = 0; ordinal < plan.Count(); ++ordinal) {
        quota.Work();
        const auto described = plan.At(ordinal);
        Require(described.has_value(), Error::kOperandMismatch, instruction.pc);
        const auto& operand = *described;
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
        Require(edge->source.value < block_map.size() &&
            proof.environments.before[method.flow.blocks[edge->source.value].first_instruction].has_value(), Error::kInvalidPhi);
        const auto& source = graph.blocks[block_map[edge->source.value].value];
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
    const auto flow = VerifyValueFlow(graph, quota.Remaining());
    if (const auto* error = std::get_if<ValueAvailabilityError>(&flow)) {
      switch (*error) {
        case ValueAvailabilityError::kWorkLimit: throw ValueImportFailure{Error::kWorkLimit, {}};
        case ValueAvailabilityError::kResourceLimit: throw ValueImportFailure{Error::kResourceLimit, {}};
        case ValueAvailabilityError::kResumeFlow: throw ValueImportFailure{Error::kResumeFlow, {}};
        case ValueAvailabilityError::kUnreachableBlock: throw ValueImportFailure{Error::kUnreachableFlow, {}};
        case ValueAvailabilityError::kInvalidPhi: throw ValueImportFailure{Error::kInvalidPhi, {}};
        case ValueAvailabilityError::kInvalidGraph:
        case ValueAvailabilityError::kInvalidTerminator:
        case ValueAvailabilityError::kUnavailableValue:
          throw ValueImportFailure{Error::kInvalidGraph, {}};
      }
    }
    quota.Work(std::get<ValueFlowProof>(flow).work);
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
