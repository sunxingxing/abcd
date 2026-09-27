#include "register_value_rule.h"

#include <type_traits>

#include "abcd/analysis/semantic_ir.h"

namespace abcd::analysis::detail {
std::variant<ValueRule, ScalarError> DescribeRegisterValue(
    const SemanticInstruction& instruction, CallResultValues calls) {
  if (!instruction.effects.normal_registers.write)
    return ScalarError::kMalformedInstruction;
  // This is the typed counterpart of the decoded rule boundary. Preserve its
  // conservative facts; adding a new IR alternative must require review here,
  // not default to Language or silently re-read its original opcode.
  return std::visit([calls](const auto& operation)
      -> std::variant<ValueRule, ScalarError> {
    using T = std::decay_t<decltype(operation)>;
    if constexpr (std::is_same_v<T, ScalarAssignment>) {
      return DescribeScalarValue(operation);
    } else if constexpr (std::is_same_v<T, NamedRead> ||
                         std::is_same_v<T, AwaitValue>) {
      return ValueRule{ValueRuleKind::kLanguageOperation, {}};
    } else if constexpr (std::is_same_v<T, LoadHole>) {
      return ValueRule{ValueRuleKind::kFixed, ValueKinds::Hole()};
    } else if constexpr (std::is_same_v<T, PushEnvironment>) {
      return ValueRule{ValueRuleKind::kFixed, ValueKinds::Environment()};
    } else if constexpr (std::is_same_v<T, StringLoad>) {
      return ValueRule{ValueRuleKind::kFixed, ValueKinds::String()};
    } else if constexpr (std::is_same_v<T, GlobalObjectLoad>) {
      return ValueRule{ValueRuleKind::kFixed, ValueKinds::Defined()};
    } else if constexpr (std::is_same_v<T, FrameThisLoad>) {
      // Neither the existence nor the entry domain of an ABI receiver slot
      // proves frame.thisObj is initialized at this instruction.
      return ValueRule{ValueRuleKind::kFixed, ValueKinds::Unknown()};
    } else if constexpr (std::is_same_v<T, ConstructorCall> ||
                         std::is_same_v<T, SuperRange> ||
                         std::is_same_v<T, ReceiverCall> ||
                         std::is_same_v<T, EmptyObjectCreation> ||
                         std::is_same_v<T, BufferedObjectCreation> ||
                         std::is_same_v<T, BufferedArrayCreation> ||
                         std::is_same_v<T, RestCopy> ||
                         std::is_same_v<T, FunctionCreation> ||
                         std::is_same_v<T, ClassCreation> ||
                         std::is_same_v<T, DefineAccessor>) {
      return ValueRule{ValueRuleKind::kFixed, ValueKinds::Language()};
    } else if constexpr (std::is_same_v<T, GlobalAccess>) {
      switch (operation.operation) {
        case GlobalOp::kReadName:
          return ValueRule{ValueRuleKind::kFixed, ValueKinds::Language()};
        case GlobalOp::kReadVar:
          return ValueRule{ValueRuleKind::kFixed, ValueKinds::Unknown()};
        case GlobalOp::kWriteName:
        case GlobalOp::kWriteVar:
        case GlobalOp::kInitConst:
        case GlobalOp::kInitMutable:
          return ScalarError::kMalformedInstruction;
      }
      return ScalarError::kMalformedInstruction;
    } else if constexpr (std::is_same_v<T, OrdinaryCall>) {
      switch (calls) {
        case CallResultValues::kUnknown:
          return ValueRule{ValueRuleKind::kFixed, ValueKinds::Unknown()};
        case CallResultValues::kLanguage:
          return ValueRule{ValueRuleKind::kFixed, ValueKinds::Language()};
      }
      return ScalarError::kMalformedInstruction;
    } else if constexpr (std::is_same_v<T, ModuleLoad> ||
                         std::is_same_v<T, LexicalLoad> ||
                         std::is_same_v<T, CurrentEnvironment>) {
      return ValueRule{ValueRuleKind::kFixed, ValueKinds::Unknown()};
    } else {
      static_assert(std::is_same_v<T, NoOperation> ||
                    std::is_same_v<T, DebuggerOperation> ||
                    std::is_same_v<T, NamedWrite> ||
                    std::is_same_v<T, OwnWrite> ||
                    std::is_same_v<T, IndexedWrite> ||
                    std::is_same_v<T, ValueWrite> ||
                    std::is_same_v<T, HoleGuard> ||
                    std::is_same_v<T, BranchOperation> ||
                    std::is_same_v<T, JumpOperation> ||
                    std::is_same_v<T, ReturnOperation> ||
                    std::is_same_v<T, ThrowOperation> ||
                    std::is_same_v<T, ModuleStore> ||
                    std::is_same_v<T, LexicalStore> ||
                    std::is_same_v<T, PopEnvironment> ||
                    std::is_same_v<T, SuperCheck>);
      return ScalarError::kMalformedInstruction;
    }
  }, instruction.operation);
}
}  // namespace abcd::analysis::detail
