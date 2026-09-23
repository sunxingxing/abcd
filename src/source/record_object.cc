#include "record_object.h"

#include <ostream>
#include <type_traits>

#include "abcd/source/scalar_expression.h"
#include "abcd/source/string_literal.h"

namespace abcd::source {
namespace {
class ObjectWriter {
 public:
  ObjectWriter(std::ostream& output, EmitBudget budget)
      : output_(output), remaining_(budget.bytes) {}

  std::variant<std::size_t, EmitError> Write(
      std::span<const RecordObjectField> fields) {
    try {
      Text("({");
      for (std::size_t i = 0; i < fields.size(); ++i) {
        if (i) Text(", ");
        Text("[");
        Account(WriteStringLiteral(fields[i].key, output_, {remaining_}));
        Text("]: ");
        Atom(fields[i].value);
      }
      Text("})");
      return written_;
    } catch (EmitError error) {
      return error;
    } catch (const std::ios_base::failure&) {
      return EmitError::kWrite;
    }
  }

 private:
  void Text(std::string_view value) {
    if (value.size() > remaining_) throw EmitError::kOutputLimit;
    output_.write(value.data(), static_cast<std::streamsize>(value.size()));
    if (!output_) throw EmitError::kWrite;
    remaining_ -= value.size();
    written_ += value.size();
  }
  void Account(const std::variant<std::size_t, EmitError>& result) {
    if (const auto* error = std::get_if<EmitError>(&result)) throw *error;
    const auto bytes = std::get<std::size_t>(result);
    remaining_ -= bytes;
    written_ += bytes;
  }
  void Atom(const StaticAtom& atom) {
    std::visit(
        [&](const auto& value) {
          using T = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<T, StaticUndefined>)
            Text("void 0");
          else if constexpr (std::is_same_v<T, StaticNull>)
            Text("null");
          else if constexpr (std::is_same_v<T, bool>)
            Text(value ? "true" : "false");
          else if constexpr (std::is_same_v<T, std::u16string>)
            Account(WriteStringLiteral(value, output_, {remaining_}));
          else {
            const analysis::ScalarValue scalar = [&]() -> analysis::ScalarValue {
              if constexpr (std::is_same_v<T, std::int32_t>)
                return analysis::IntegerLiteral{value};
              else
                return analysis::Float64Bits{value.bits};
            }();
            Account(WriteScalarExpression(scalar, {"", {}}, output_,
                                          {remaining_}));
          }
        },
        atom);
  }
  std::ostream& output_;
  std::size_t remaining_;
  std::size_t written_ = 0;
};
}  // namespace

std::variant<std::size_t, EmitError> WriteRecordObject(
    std::span<const RecordObjectField> fields, std::ostream& output,
    EmitBudget budget) {
  return ObjectWriter(output, budget).Write(fields);
}
}  // namespace abcd::source
