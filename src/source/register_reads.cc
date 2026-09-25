#include "register_reads.h"

#include <type_traits>

namespace abcd::source {
namespace {
RegisterRead Read(bool present) {
  return present ? RegisterRead::kPresent : RegisterRead::kAbsent;
}
bool Contains(const analysis::CallRange& range, abc::RegisterId reg) {
  return reg.value >= range.first.value &&
      reg.value - range.first.value < range.count;
}
bool Contains(const std::variant<analysis::CallRegisters, analysis::CallRange>& args,
              abc::RegisterId reg) {
  return std::visit([reg](const auto& value) {
    using T = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<T, analysis::CallRange>) {
      return Contains(value, reg);
    } else {
      static_assert(std::is_same_v<T, analysis::CallRegisters>);
      if (value.count > value.values.size()) return true;
      for (std::size_t i = 0; i < value.count; ++i)
        if (value.values[i] == reg) return true;
      return false;
    }
  }, args);
}
bool ReadsScalar(const analysis::ScalarExpression& expression,
                 abc::RegisterId reg) {
  return std::visit([reg](const auto& value) {
    using T = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<T, analysis::ScalarValue>) {
      return ReadsRegister(value, reg);
    } else if constexpr (std::is_same_v<T, analysis::UnaryExpression>) {
      return ReadsRegister(value.input, reg);
    } else if constexpr (std::is_same_v<T, analysis::BinaryExpression>) {
      return ReadsRegister(value.left, reg) || ReadsRegister(value.right, reg);
    } else {
      static_assert(std::is_same_v<T, analysis::PropertyRead>);
      return ReadsRegister(value.receiver, reg) || ReadsRegister(value.key, reg);
    }
  }, expression);
}
}  // namespace

bool ReadsRegister(const analysis::ScalarValue& value, abc::RegisterId reg) {
  const auto* read = std::get_if<abc::RegisterId>(&value);
  return read && *read == reg;
}
RegisterRead ReadsRegister(const SourceExpression& expression,
                           abc::RegisterId reg) {
  return std::visit([reg](const auto& value) -> RegisterRead {
    using T = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<T, analysis::ScalarExpression>) {
      return Read(ReadsScalar(value, reg));
    } else if constexpr (std::is_same_v<T, MemberExpression>) {
      if (value.receiver) return RegisterRead::kUnknown;
      return Read(ReadsRegister(value.object, reg));
    } else if constexpr (std::is_same_v<T, StringComparison>) {
      return Read(ReadsRegister(value.left, reg));
    } else if constexpr (std::is_same_v<T, CallExpression>) {
      return Read(Contains(value.call.arguments, reg));
    } else if constexpr (std::is_same_v<T, MemberCallExpression>) {
      return Read(value.call.receiver == reg || Contains(value.call.arguments, reg));
    } else if constexpr (std::is_same_v<T, analysis::ConstructorCall>) {
      return Read(Contains(value.inputs, reg));
    } else if constexpr (std::is_same_v<T, StringExpression> ||
                         std::is_same_v<T, ModuleExpression> ||
                         std::is_same_v<T, GlobalExpression> ||
                         std::is_same_v<T, LocalExpression> ||
                         std::is_same_v<T, CapturedExpression> ||
                         std::is_same_v<T, analysis::EmptyObjectCreation> ||
                         std::is_same_v<T, analysis::BufferedObjectCreation> ||
                         std::is_same_v<T, analysis::BufferedArrayCreation> ||
                         std::is_same_v<T, analysis::GlobalObjectLoad>) {
      return RegisterRead::kAbsent;
    } else {
      static_assert(std::is_same_v<T, FunctionExpression> ||
                    std::is_same_v<T, ClassExpression> ||
                    std::is_same_v<T, MethodExpression> ||
                    std::is_same_v<T, AccessorExpression> ||
                    std::is_same_v<T, analysis::RestCopy> ||
                    std::is_same_v<T, analysis::AwaitValue> ||
                    std::is_same_v<T, analysis::SuperRange>);
      return RegisterRead::kUnknown;
    }
  }, expression);
}
}  // namespace abcd::source
