#include "value_operands.h"

#include <type_traits>

namespace abcd::analysis {
namespace {
bool SameSlot(const ScalarSlot& left, const ScalarSlot& right) {
  if (left.index() != right.index()) return false;
  const auto* reg = std::get_if<abc::RegisterId>(&left);
  return !reg || *reg == std::get<abc::RegisterId>(right);
}

ScalarValue SlotValue(const ScalarSlot& slot) {
  return std::visit([](const auto& value) -> ScalarValue { return value; }, slot);
}

void Append(ValueOperands& plan, OperandRole role, ScalarValue value,
            ReadPurpose purpose = ReadPurpose::kOperand) {
  plan.operands[plan.count++] = {role, std::move(value), purpose, {}};
}

void Expression(ValueOperands& plan, const ScalarExpression& expression) {
  std::visit([&](const auto& value) {
    using T = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<T, ScalarValue>) {
      Append(plan, OperandRole::kInput, value);
    } else if constexpr (std::is_same_v<T, UnaryExpression>) {
      Append(plan, OperandRole::kInput, value.input);
    } else if constexpr (std::is_same_v<T, BinaryExpression>) {
      Append(plan, OperandRole::kLeft, value.left);
      Append(plan, OperandRole::kRight, value.right);
    } else if constexpr (std::is_same_v<T, PropertyRead>) {
      Append(plan, OperandRole::kBase, value.receiver);
      Append(plan, OperandRole::kKey, value.key);
    }
  }, expression);
}
}  // namespace

std::variant<ValueOperands, OperandError> DescribeValueOperands(
    const SemanticInstruction& instruction) {
  ValueOperands plan;
  const bool supported = std::visit([&](const auto& op) {
    using T = std::decay_t<decltype(op)>;
    if constexpr (std::is_same_v<T, ScalarAssignment>) {
      Expression(plan, op.expression);
      plan.write = op.destination;
    } else if constexpr (std::is_same_v<T, AwaitValue> || std::is_same_v<T, YieldValue>) {
      Append(plan, OperandRole::kInput, op.input);
      plan.write = Accumulator{};
    } else if constexpr (std::is_same_v<T, NamedRead>) {
      Append(plan, OperandRole::kBase, Accumulator{});
      plan.write = Accumulator{};
    } else if constexpr (std::is_same_v<T, IterResult>) {
      Append(plan, OperandRole::kInput, op.value);
      Append(plan, OperandRole::kCondition, op.done);
      plan.write = Accumulator{};
    } else if constexpr (std::is_same_v<T, ApplyConstructorCall>) {
      Append(plan, OperandRole::kConstructor, op.constructor);
      Append(plan, OperandRole::kInput, Accumulator{});
      plan.write = Accumulator{};
    } else if constexpr (std::is_same_v<T, NamedWrite> || std::is_same_v<T, OwnWrite> ||
                         std::is_same_v<T, IndexedWrite>) {
      Append(plan, OperandRole::kBase, op.object);
      Append(plan, OperandRole::kStoredValue, Accumulator{});
    } else if constexpr (std::is_same_v<T, DefineAccessor>) {
      Append(plan, OperandRole::kBase, op.object);
      Append(plan, OperandRole::kKey, op.key);
      Append(plan, OperandRole::kGetter, op.getter);
      Append(plan, OperandRole::kSetter, op.setter);
      Append(plan, OperandRole::kNameFlag, Accumulator{}, ReadPurpose::kProtocol);
      plan.write = Accumulator{};
    } else if constexpr (std::is_same_v<T, ValueWrite>) {
      Append(plan, OperandRole::kBase, op.object);
      Append(plan, OperandRole::kKey, op.key);
      Append(plan, OperandRole::kStoredValue, Accumulator{});
    } else if constexpr (std::is_same_v<T, BranchOperation>) {
      Append(plan, OperandRole::kCondition, op.predicate.input);
    } else if constexpr (std::is_same_v<T, ReturnOperation>) {
      Append(plan, OperandRole::kReturnValue, op.value);
    } else if constexpr (std::is_same_v<T, ThrowOperation>) {
      Append(plan, OperandRole::kThrownValue, op.value);
    } else if constexpr (std::is_same_v<T, LoadHole> || std::is_same_v<T, FrameThisLoad> ||
                         std::is_same_v<T, FrameFunctionLoad> ||
                         std::is_same_v<T, GlobalObjectLoad> ||
                         std::is_same_v<T, SymbolConstructorLoad> || std::is_same_v<T, RestCopy> ||
                         std::is_same_v<T, StringLoad> || std::is_same_v<T, EmptyObjectCreation> ||
                         std::is_same_v<T, BufferedObjectCreation> || std::is_same_v<T, BufferedArrayCreation> ||
                         std::is_same_v<T, ModuleLoad>) {
      plan.write = Accumulator{};
    } else if constexpr (std::is_same_v<T, LexicalLoad> || std::is_same_v<T, PushEnvironment> ||
                         std::is_same_v<T, CurrentEnvironment>) {
      plan.write = Accumulator{};
    } else if constexpr (std::is_same_v<T, LexicalStore>) {
      Append(plan, OperandRole::kStoredValue, SlotValue(op.value));
    } else if constexpr (std::is_same_v<T, ModuleStore>) {
      Append(plan, OperandRole::kStoredValue, Accumulator{});
    } else if constexpr (std::is_same_v<T, PopEnvironment>) {
      // The implicit environment changes; the accumulator does not.
    } else if constexpr (std::is_same_v<T, ClassCreation>) {
      // The register carries a VM heritage input, including Hole for no extends.
      // Keep its protocol use distinct from a source constructor call operand.
      Append(plan, OperandRole::kHeritage, op.parent, ReadPurpose::kProtocol);
      plan.write = Accumulator{};
    } else if constexpr (std::is_same_v<T, FunctionCreation>) {
      // DEFINEFUNC obtains its home object from the current function implicitly;
      // DEFINEMETHOD reads acc as a VM creation input, not a JS call argument.
      switch (op.home_object) {
        case HomeObjectInput::kCurrentFunction: break;
        case HomeObjectInput::kAccumulator:
          Append(plan, OperandRole::kHomeObject, Accumulator{}, ReadPurpose::kProtocol);
          break;
        default: return false;
      }
      plan.write = Accumulator{};
    } else if constexpr (std::is_same_v<T, GlobalAccess>) {
      switch (op.operation) {
        case GlobalOp::kReadName: case GlobalOp::kReadVar:
          plan.write = Accumulator{};
          break;
        case GlobalOp::kWriteName: case GlobalOp::kWriteVar:
        case GlobalOp::kInitConst: case GlobalOp::kInitMutable:
          return false;
        default: return false;
      }
    } else if constexpr (std::is_same_v<T, SuperForward>) {
      Append(plan, OperandRole::kConstructor, op.constructor, ReadPurpose::kProtocol);
      plan.write = Accumulator{};
    } else if constexpr (std::is_same_v<T, SuperSpread>) {
      // The accumulator is the actual current constructor, not the parent.
      // The register holds one aggregate argument array, not one JS argument.
      Append(plan, OperandRole::kConstructor, Accumulator{});
      Append(plan, OperandRole::kInput, op.arguments);
      plan.write = Accumulator{};
    } else if constexpr (std::is_same_v<T, SuperCheck>) {
      Append(plan, OperandRole::kGuardValue, Accumulator{}, ReadPurpose::kProtocol);
    } else if constexpr (std::is_same_v<T, HoleGuard>) {
      Append(plan, OperandRole::kGuardValue, SlotValue(op.value));
      // The diagnostic register is read only on the failing guard path.
      // Preserve the protocol dependency; never make it an eager JS operand.
      if (const auto* name = std::get_if<abc::RegisterId>(&op.name))
        Append(plan, OperandRole::kDiagnosticName, *name, ReadPurpose::kProtocol);
    } else if constexpr (std::is_same_v<T, NoOperation> ||
                         std::is_same_v<T, DebuggerOperation> ||
                         std::is_same_v<T, JumpOperation>) {
    } else {
      return false;
    }
    return true;
  }, instruction.operation);
  if (!supported) return OperandError::kUnsupportedOperation;
  const auto& effect = instruction.effects.normal_registers;
  if (effect.range || effect.read_count > effect.reads.size())
    return OperandError::kReadCount;
  std::uint32_t ordinal = 0;
  for (auto& operand : std::span(plan.operands).first(plan.count)) {
    std::optional<ScalarSlot> slot;
    if (std::holds_alternative<Accumulator>(operand.value))
      slot = Accumulator{};
    else if (const auto* reg = std::get_if<abc::RegisterId>(&operand.value))
      slot = *reg;
    if (!slot) continue;
    if (ordinal >= effect.read_count) return OperandError::kReadCount;
    if (!SameSlot(*slot, effect.reads[ordinal])) return OperandError::kReadSlot;
    operand.read_ordinal = ordinal++;
  }
  if (ordinal != effect.read_count) return OperandError::kReadCount;
  if (plan.write.has_value() != effect.write.has_value() ||
      (plan.write && !SameSlot(*plan.write, *effect.write)))
    return OperandError::kWriteSlot;
  return plan;
}
}  // namespace abcd::analysis
