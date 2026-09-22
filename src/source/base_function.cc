#include "abcd/source/base_function.h"

#include <array>
#include <charconv>
#include <limits>
#include <memory>
#include <ostream>
#include <streambuf>

namespace abcd::source {
namespace {
constexpr std::size_t kIndexDigits =
    std::numeric_limits<std::uint32_t>::digits10 + 1;
using RegisterName = std::array<char, kIndexDigits + 2>;
constexpr std::size_t kRegisterBytes =
    sizeof(RegisterName) + sizeof(std::string_view);
class DiscardBuffer : public std::streambuf {
  std::streamsize xsputn(const char*, std::streamsize count) override {
    return count;
  }
};

bool CanonicalFrame(const abc::FrameLayout& frame) {
  if (frame.registers < frame.locals) return false;
  const auto built = abc::BuildFrameLayout(
      frame.locals, frame.registers - frame.locals,
      {frame.this_value.has_value(), frame.new_target.has_value(),
       frame.extra_arguments, frame.function.has_value()});
  const auto* canonical = std::get_if<abc::FrameLayout>(&built);
  return canonical && canonical->function == frame.function &&
         canonical->new_target == frame.new_target &&
         canonical->this_value == frame.this_value &&
         canonical->first_parameter == frame.first_parameter &&
         canonical->parameters == frame.parameters;
}

class FunctionWriter {
 public:
  FunctionWriter(const BaseFunction& function, ScalarNames names,
                 std::ostream& output, LocalLexicalBudget budget,
                 const MethodConstants* constants)
      : function_(function),
        names_(names),
        output_(output),
        budget_(budget),
        constants_(constants) {}

  FunctionEmitResult Write(const abc::MethodBody& body) {
    try {
      Text("(function ");
      Text(function_.name);
      Text("(");
      const auto& frame = function_.frame;
      for (std::uint32_t i = 0; i < frame.parameters && !error_; ++i) {
        if (i != 0) Text(", ");
        Parameter(i);
      }
      Text(") {\n\"use strict\";\nlet $acc;\n");
      // Each VM register is independently writable. In particular, a function
      // slot must not alias the immutable named-expression self binding.
      for (std::uint32_t i = 0; i < frame.registers && !error_; ++i) {
        Text("let ");
        Text(names_.registers[i]);
        const abc::RegisterId slot{i};
        if (frame.function == slot) {
          Text(" = ");
          Text(function_.name);
        } else if (frame.new_target == slot) {
          Text(" = new.target");
        } else if (frame.this_value == slot) {
          Text(" = this");
        } else if (i >= frame.first_parameter.value) {
          Text(" = ");
          Parameter(i - frame.first_parameter.value);
        }
        Text(";\n");
      }
      if (error_) return *error_;
      auto body_budget = budget_;
      body_budget.output.bytes -= written_;
      const auto result =
          WriteLocalLexicalBody(body, names_, output_, body_budget, constants_);
      if (const auto* bytes = std::get_if<std::size_t>(&result))
        written_ += *bytes;
      else
        return std::visit(
            [](auto value) -> FunctionEmitResult { return value; }, result);
      Text("})");
      if (error_) return *error_;
      return written_;
    } catch (const std::ios_base::failure&) {
      return EmitError::kWrite;
    }
  }

 private:
  void Text(std::string_view text) {
    if (error_) return;
    if (text.size() > budget_.output.bytes - written_) {
      error_ = EmitError::kOutputLimit;
      return;
    }
    output_.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output_) {
      error_ = EmitError::kWrite;
      return;
    }
    written_ += text.size();
  }
  void Parameter(std::uint32_t index) {
    Text("$p");
    std::array<char, kIndexDigits> digits{};
    const auto end =
        std::to_chars(digits.data(), digits.data() + digits.size(), index).ptr;
    Text({digits.data(), static_cast<std::size_t>(end - digits.data())});
  }
  const BaseFunction& function_;
  ScalarNames names_;
  std::ostream& output_;
  LocalLexicalBudget budget_;
  const MethodConstants* constants_;
  std::size_t written_ = 0;
  std::optional<EmitError> error_;
};
}  // namespace

FunctionEmitResult WriteBaseFunction(const BaseFunction& function,
                                     const abc::MethodBody& body,
                                     std::ostream& output,
                                     LocalLexicalBudget budget,
                                     const MethodConstants* constants) {
  if (function.creation.kind != analysis::CreationKind::kFunction ||
      function.creation.home_object !=
          analysis::HomeObjectInput::kCurrentFunction)
    return FunctionEmitError::kCreation;
  if (function.attributes.sendable) return FunctionEmitError::kSendable;
  if (function.attributes.kind != abc::FunctionKind::kBaseConstructor)
    return FunctionEmitError::kKind;
  if (!CanonicalFrame(function.frame)) return FunctionEmitError::kFrame;
  if (function.creation.length.value != function.frame.parameters)
    return FunctionEmitError::kLength;
  // Reuse the expression emitter's strict-safe binding validation. '$' is our
  // private source namespace; never let a supplied function name shadow it.
  DiscardBuffer buffer;
  std::ostream discard(&buffer);
  if (function.name.starts_with('$')) return FunctionEmitError::kName;
  const auto name_check =
      WriteScalarExpression(analysis::ScalarValue{analysis::Accumulator{}},
                            {function.name, {}}, discard, budget.output);
  if (const auto* error = std::get_if<EmitError>(&name_check)) {
    if (*error == EmitError::kInvalidBinding) return FunctionEmitError::kName;
    return *error;
  }
  const auto count = function.frame.registers;
  if (count > budget.bytes / kRegisterBytes)
    return FunctionEmitError::kResourceLimit;
  budget.bytes -= count * kRegisterBytes;
  try {
    auto storage = std::make_unique<RegisterName[]>(count);
    auto names = std::make_unique<std::string_view[]>(count);
    for (std::uint32_t i = 0; i < count; ++i) {
      auto& name = storage[i];
      name[0] = '$';
      name[1] = 'r';
      const auto end =
          std::to_chars(name.data() + 2, name.data() + name.size(), i).ptr;
      names[i] = {name.data(), static_cast<std::size_t>(end - name.data())};
    }
    const ScalarNames bindings{"$acc", {names.get(), count}};
    const auto checked =
        FunctionWriter(function, bindings, discard, budget, constants)
            .Write(body);
    if (!std::holds_alternative<std::size_t>(checked)) return checked;
    return FunctionWriter(function, bindings, output, budget, constants)
        .Write(body);
  } catch (const std::bad_alloc&) {
    return FunctionEmitError::kResourceLimit;
  } catch (const std::length_error&) {
    return FunctionEmitError::kResourceLimit;
  }
}
}  // namespace abcd::source
