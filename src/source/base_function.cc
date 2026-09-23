#include "abcd/source/base_function.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <memory>
#include <ostream>
#include <streambuf>
#include <string>
#include <vector>

#include "abcd/abc/register_access.h"
#include "abcd/source/base_constructor.h"
#include "abcd/source/class_method.h"
#include "abcd/source/string_literal.h"
#include "capture_stream.h"
#include "branch_value_proof.h"
#include "function_template.h"

namespace abcd::source {
namespace {
enum class CallableSyntax {
  kFunctionExpression,
  kAsyncFunctionExpression,
  kArrowExpression,
  kBaseConstructor,
  kClassMethod
};
struct CallableShape {
  abc::FrameLayout frame;
  std::string_view name;
  std::uint16_t length;
  CallableSyntax syntax;
  std::u16string_view key{};
  abc::ClassPlacement placement = abc::ClassPlacement::kPrototype;
  std::u16string_view arrow_name{};
};
constexpr std::size_t kIndexDigits =
    std::numeric_limits<std::uint32_t>::digits10 + 1;
using RegisterName = std::array<char, kIndexDigits + 2>;
constexpr std::size_t kRegisterBytes =
    sizeof(RegisterName) + sizeof(std::string_view);

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

bool ObservesArrowCallContext(const abc::MethodBody& body,
                              const abc::FrameLayout& frame) {
  constexpr std::size_t kCallContextSlots = 2;
  const auto contains = [](const abc::RegisterWindow& window,
                           abc::RegisterId slot) {
    return slot.value >= window.first.value &&
           slot.value - window.first.value < window.count;
  };
  std::array<abc::RegisterId, kCallContextSlots> copy_destinations{};
  std::size_t copy_count = 0;
  for (const auto& site : body.instructions) {
    using abc::Opcode;
    switch (site.decoded.opcode) {
      case Opcode::kLDTHIS:
      case Opcode::kLDNEWTARGET:
      case Opcode::kLDTHISBYNAME_IMM8_ID16:
      case Opcode::kLDTHISBYNAME_IMM16_ID16:
      case Opcode::kSTTHISBYNAME_IMM8_ID16:
      case Opcode::kSTTHISBYNAME_IMM16_ID16:
      case Opcode::kLDTHISBYVALUE_IMM8:
      case Opcode::kLDTHISBYVALUE_IMM16:
      case Opcode::kSTTHISBYVALUE_IMM8_V8:
      case Opcode::kSTTHISBYVALUE_IMM16_V8:
        return true;
      default:
        break;
    }
    const auto accesses =
        abc::GetRegisterAccesses(site.decoded, frame.registers);
    const auto* known = std::get_if<abc::RegisterAccesses>(&accesses);
    if (!known) return true;
    for (std::size_t i = 0; i < known->count; ++i) {
      const auto& window = known->windows[i];
      if (window.access == abc::OperandAccess::kWrite) continue;
      const bool context_read =
          (frame.this_value && contains(window, *frame.this_value)) ||
          (frame.new_target && contains(window, *frame.new_target));
      if (!context_read) continue;
      // The v7 compiler copies hidden ABI inputs into local slots at entry.
      // A pure MOV is unobservable when its destination has no reads anywhere
      // in the body; this conservative whole-body check also covers branches.
      const bool move = site.decoded.opcode == Opcode::kMOV_V4_V4 ||
                        site.decoded.opcode == Opcode::kMOV_V8_V8;
      if (!move || known->count != 2 || i != 1 ||
          known->windows[0].access != abc::OperandAccess::kWrite ||
          known->windows[0].count != 1 || window.count != 1 ||
          copy_count == copy_destinations.size())
        return true;
      copy_destinations[copy_count++] = known->windows[0].first;
    }
  }
  for (const auto& site : body.instructions) {
    const auto accesses =
        abc::GetRegisterAccesses(site.decoded, frame.registers);
    const auto* known = std::get_if<abc::RegisterAccesses>(&accesses);
    if (!known) return true;
    for (std::size_t i = 0; i < known->count; ++i) {
      const auto& window = known->windows[i];
      if (window.access == abc::OperandAccess::kWrite) continue;
      for (std::size_t copy = 0; copy < copy_count; ++copy)
        if (contains(window, copy_destinations[copy])) return true;
    }
  }
  return false;
}

class FunctionWriter {
 public:
  FunctionWriter(const CallableShape& function, ScalarNames names,
                 std::ostream& output, LocalLexicalBudget budget,
                 const MethodConstants* constants,
                 const abc::MethodLookup* methods,
                 analysis::CapturedValues captured_values)
      : function_(function),
        names_(names),
        output_(output),
        budget_(budget),
        constants_(constants),
        methods_(methods),
        captured_values_(captured_values) {}

  FunctionEmitResult Write(const abc::MethodBody& body) {
    try {
      if (function_.syntax == CallableSyntax::kClassMethod) {
        if (function_.placement == abc::ClassPlacement::kStatic)
          Text("static ");
        Text("[");
        if (error_) return *error_;
        const auto key = WriteStringLiteral(function_.key, output_,
                                            {budget_.output.bytes - written_});
        if (const auto* error = std::get_if<EmitError>(&key)) return *error;
        written_ += std::get<std::size_t>(key);
        Text("](");
      } else if (function_.syntax == CallableSyntax::kBaseConstructor) {
        Text("constructor(");
      } else if (function_.syntax == CallableSyntax::kArrowExpression) {
        // A computed data-property name applies SetFunctionName without a
        // mutable intrinsic; arrow IIFEs retain lexical this/new.target.
        Text("(() => {\nconst $arrow = ({[");
        ArrowName();
        Text("]: (");
      } else {
        Text(function_.syntax == CallableSyntax::kAsyncFunctionExpression
                 ? "(async function " : "(function ");
        Text(function_.name);
        Text("(");
      }
      const auto& frame = function_.frame;
      for (std::uint32_t i = 0; i < frame.parameters && !error_; ++i) {
        if (i != 0) Text(", ");
        Parameter(i);
      }
      Text(function_.syntax == CallableSyntax::kArrowExpression
               ? ") => {\n\"use strict\";\nlet $acc;\n"
               : ") {\n\"use strict\";\nlet $acc;\n");
      // Each VM register is independently writable. In particular, a function
      // slot must not alias the immutable named-expression self binding.
      for (std::uint32_t i = 0; i < frame.registers && !error_; ++i) {
        Text("let ");
        Text(names_.registers[i]);
        const abc::RegisterId slot{i};
        if (frame.function == slot) {
          Text(" = ");
          Text(function_.syntax == CallableSyntax::kArrowExpression
                   ? "$arrow"
                   : function_.name);
        } else if (frame.new_target == slot) {
          if (function_.syntax != CallableSyntax::kArrowExpression)
            Text(" = new.target");
        } else if (frame.this_value == slot) {
          if (function_.syntax != CallableSyntax::kArrowExpression)
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
      std::optional<MethodConstants> scoped_constants;
      const MethodConstants* body_constants = constants_;
      if (constants_ && constants_->global_scope) {
        scoped_constants.emplace(*constants_);
        if (function_.syntax == CallableSyntax::kFunctionExpression ||
            function_.syntax == CallableSyntax::kAsyncFunctionExpression)
          scoped_constants->global_scope->self_binding = function_.name;
        body_constants = &*scoped_constants;
      }
      const auto lexical =
          captured_values_ == analysis::CapturedValues::kLanguage
              ? WriteLocalLexicalTemplate(body, names_, output_, body_budget,
                                          body_constants)
              : WriteLocalLexicalBody(body, names_, output_, body_budget,
                                      body_constants, methods_);
      FunctionEmitResult result = std::visit(
          [](auto value) -> FunctionEmitResult { return value; }, lexical);
      if (const auto* failure =
              std::get_if<analysis::LinearLexicalFailure>(&lexical);
          failure && failure->kind == analysis::LinearLexicalError::kControlFlow &&
          captured_values_ == analysis::CapturedValues::kReject) {
        // The ordered path has no lexical-environment synthesis. It is only a
        // fallback after the straight-line planner rejects control flow; its
        // own opcode proof must accept every live instruction before emission.
        AcyclicBudget ordered_budget;
        ordered_budget.bytes = body_budget.bytes;
        ordered_budget.output = body_budget.output;
        if (const auto unsafe = ProveBranchValues(
                body, names_.registers.size(), body_budget))
          return *unsafe;
        result = std::visit(
            [](auto value) -> FunctionEmitResult { return value; },
            WriteBlockLoopBody(body, names_, output_, ordered_budget,
                               body_constants));
      }
      if (const auto* bytes = std::get_if<std::size_t>(&result))
        written_ += *bytes;
      else
        return result;
      if (function_.syntax == CallableSyntax::kArrowExpression) {
        Text("}})[");
        ArrowName();
        Text("];\nreturn $arrow;\n})()");
      } else {
        Text(function_.syntax == CallableSyntax::kFunctionExpression ||
                     function_.syntax == CallableSyntax::kAsyncFunctionExpression
                 ? "})" : "}");
      }
      if (error_) return *error_;
      return written_;
    } catch (const std::ios_base::failure&) {
      return EmitError::kWrite;
    }
  }

 private:
  void ArrowName() {
    if (error_) return;
    const auto result = WriteStringLiteral(
        function_.arrow_name, output_, {budget_.output.bytes - written_});
    if (const auto* failure = std::get_if<EmitError>(&result))
      error_ = *failure;
    else
      written_ += std::get<std::size_t>(result);
  }
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
    if (constants_ && !constants_->source_parameters.empty()) {
      Text(constants_->source_parameters[index]);
      return;
    }
    Text("$p");
    std::array<char, kIndexDigits> digits{};
    const auto end =
        std::to_chars(digits.data(), digits.data() + digits.size(), index).ptr;
    Text({digits.data(), static_cast<std::size_t>(end - digits.data())});
  }
  const CallableShape& function_;
  ScalarNames names_;
  std::ostream& output_;
  LocalLexicalBudget budget_;
  const MethodConstants* constants_;
  const abc::MethodLookup* methods_;
  analysis::CapturedValues captured_values_;
  std::size_t written_ = 0;
  std::optional<EmitError> error_;
};
FunctionEmitResult WriteCallable(const CallableShape& function,
                                 const abc::MethodBody& body,
                                 std::ostream& output,
                                 LocalLexicalBudget budget,
                                 const MethodConstants* constants,
                                 const abc::MethodLookup* methods,
                                 analysis::CapturedValues captured_values) {
  if (!CanonicalFrame(function.frame)) return FunctionEmitError::kFrame;
  if (function.length != function.frame.parameters)
    return FunctionEmitError::kLength;
  // Reuse the expression emitter's strict-safe binding validation. '$' is our
  // private source namespace; never let a supplied function name shadow it.
  CaptureDiscard buffer;
  std::ostream discard(&buffer);
  if (function.syntax != CallableSyntax::kArrowExpression) {
    if (function.name.starts_with('$')) return FunctionEmitError::kName;
    const auto name_check =
        WriteScalarExpression(analysis::ScalarValue{analysis::Accumulator{}},
                              {function.name, {}}, discard, budget.output);
    if (const auto* error = std::get_if<EmitError>(&name_check)) {
      if (*error == EmitError::kInvalidBinding) return FunctionEmitError::kName;
      return *error;
    }
  }
  const auto count = function.frame.registers;
  if (count > budget.bytes / kRegisterBytes)
    return FunctionEmitError::kResourceLimit;
  budget.bytes -= count * kRegisterBytes;
  try {
    std::vector<std::string_view> sorted_parameters;
    std::optional<MethodConstants> named_constants;
    if (constants && !constants->source_parameters.empty()) {
      const auto parameters = constants->source_parameters;
      if (parameters.size() != function.frame.parameters)
        return FunctionEmitError::kName;
      if (parameters.size() > budget.bytes / sizeof(std::string_view))
        return FunctionEmitError::kResourceLimit;
      budget.bytes -= parameters.size() * sizeof(std::string_view);
      sorted_parameters.assign(parameters.begin(), parameters.end());
      if (sorted_parameters.capacity() > parameters.size() &&
          sorted_parameters.capacity() - parameters.size() >
              budget.bytes / sizeof(std::string_view))
        return FunctionEmitError::kResourceLimit;
      budget.bytes -= (sorted_parameters.capacity() - parameters.size()) *
                      sizeof(std::string_view);
      for (const auto name : sorted_parameters) {
        if (name.starts_with('$') || name == function.name ||
            name == "ClassValue" || name.starts_with("MethodSelf"))
          return FunctionEmitError::kName;
        const auto valid = WriteScalarExpression(
            analysis::ScalarValue{analysis::Accumulator{}}, {name, {}},
            discard, {SIZE_MAX});
        if (!std::holds_alternative<std::size_t>(valid))
          return FunctionEmitError::kName;
      }
      std::sort(sorted_parameters.begin(), sorted_parameters.end());
      if (std::adjacent_find(sorted_parameters.begin(),
                             sorted_parameters.end()) !=
          sorted_parameters.end())
        return FunctionEmitError::kName;
      if (constants->global_scope) {
        named_constants.emplace(*constants);
        named_constants->global_scope->parameter_bindings = sorted_parameters;
        constants = &*named_constants;
      }
    }
    std::u16string arrow_name;
    if (function.syntax == CallableSyntax::kArrowExpression) {
      constexpr unsigned char kAsciiLimit = 0x80;
      if (function.name.size() > budget.bytes / sizeof(char16_t))
        return FunctionEmitError::kResourceLimit;
      arrow_name.reserve(function.name.size());
      for (const unsigned char byte : function.name) {
        if (byte == 0 || byte >= kAsciiLimit)
          return FunctionEmitError::kName;
        arrow_name.push_back(byte);
      }
      if (arrow_name.capacity() > budget.bytes / sizeof(char16_t))
        return FunctionEmitError::kResourceLimit;
      const auto retained = arrow_name.capacity() * sizeof(char16_t);
      budget.bytes -= retained;
    }
    auto shape = function;
    shape.arrow_name = arrow_name;
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
    const auto checked = FunctionWriter(shape, bindings, discard, budget,
                                        constants, methods, captured_values)
                             .Write(body);
    if (!std::holds_alternative<std::size_t>(checked)) return checked;
    return FunctionWriter(shape, bindings, output, budget, constants,
                          methods, captured_values)
        .Write(body);
  } catch (const std::bad_alloc&) {
    return FunctionEmitError::kResourceLimit;
  } catch (const std::length_error&) {
    return FunctionEmitError::kResourceLimit;
  }
}
FunctionEmitResult WriteFunction(const BaseFunction& function,
                                 const abc::MethodBody& body,
                                 std::ostream& output,
                                 LocalLexicalBudget budget,
                                 const MethodConstants* constants,
                                 const abc::MethodLookup* methods,
                                 analysis::CapturedValues captured_values) {
  if (function.creation.kind != analysis::CreationKind::kFunction ||
      function.creation.home_object !=
          analysis::HomeObjectInput::kCurrentFunction)
    return FunctionEmitError::kCreation;
  if (function.attributes.sendable) return FunctionEmitError::kSendable;
  if (function.attributes.kind != abc::FunctionKind::kBaseConstructor &&
      function.attributes.kind != abc::FunctionKind::kAsync)
    return FunctionEmitError::kKind;
  // A v7 ASYNC_FUNCTION created by DEFINEFUNC has async call semantics;
  // the body verifier still refuses await/resume opcodes it cannot recover.
  const auto syntax = function.attributes.kind == abc::FunctionKind::kAsync
                          ? CallableSyntax::kAsyncFunctionExpression
                          : CallableSyntax::kFunctionExpression;
  return WriteCallable(
      {function.frame, function.name, function.creation.length.value,
       syntax},
      body, output, budget, constants, methods, captured_values);
}
}  // namespace
FunctionEmitResult WriteClassMethod(const ClassMethod& method,
                                    const abc::MethodBody& body,
                                    std::ostream& output,
                                    LocalLexicalBudget budget,
                                    const MethodConstants* constants,
                                    const abc::MethodLookup* methods) {
  if (method.attributes.sendable) return FunctionEmitError::kSendable;
  if (method.attributes.kind != abc::FunctionKind::kNone ||
      method.member.kind != abc::ClassMethodKind::kOrdinary ||
      method.effective_kind != EffectiveMethodKind::kOrdinary)
    return FunctionEmitError::kKind;
  return WriteCallable(
      {method.frame, method.self_binding, method.member.length,
       CallableSyntax::kClassMethod, method.key, method.placement},
      body, output, budget, constants, methods,
      analysis::CapturedValues::kReject);
}
FunctionEmitResult WriteBaseConstructor(const BaseConstructor& constructor,
                                        const abc::MethodBody& body,
                                        std::ostream& output,
                                        LocalLexicalBudget budget,
                                        const MethodConstants* constants,
                                        const abc::MethodLookup* methods) {
  if (constructor.attributes.sendable) return FunctionEmitError::kSendable;
  return WriteCallable(
      {constructor.frame, constructor.class_binding,
       constructor.creation.length, CallableSyntax::kBaseConstructor},
      body, output, budget, constants, methods,
      analysis::CapturedValues::kReject);
}
FunctionEmitResult WriteBaseFunction(const BaseFunction& function,
                                     const abc::MethodBody& body,
                                     std::ostream& output,
                                     LocalLexicalBudget budget,
                                     const MethodConstants* constants,
                                     const abc::MethodLookup* methods) {
  return WriteFunction(function, body, output, budget, constants, methods,
                       analysis::CapturedValues::kReject);
}
FunctionEmitResult WriteBaseFunctionTemplate(const BaseFunction& function,
                                             const abc::MethodBody& body,
                                             std::ostream& output,
                                             LocalLexicalBudget budget,
                                             const MethodConstants* constants) {
  return WriteFunction(function, body, output, budget, constants, nullptr,
                       analysis::CapturedValues::kLanguage);
}
FunctionEmitResult WriteArrowFunction(
    const BaseFunction& function, const abc::MethodBody& body,
    std::ostream& output, LocalLexicalBudget budget,
    const MethodConstants* constants, analysis::CapturedValues captured_values) {
  if (function.creation.kind != analysis::CreationKind::kFunction ||
      function.creation.home_object !=
          analysis::HomeObjectInput::kCurrentFunction)
    return FunctionEmitError::kCreation;
  if (function.attributes.sendable) return FunctionEmitError::kSendable;
  if (function.attributes.kind != abc::FunctionKind::kArrow)
    return FunctionEmitError::kKind;
  // Pinned interpreter Execute copies call-info this/newTarget into ABI slots.
  // A native arrow cannot expose those dynamic slots; lexical accesses use
  // separate environment operations and are handled by the body writer.
  if (ObservesArrowCallContext(body, function.frame))
    return FunctionEmitError::kFrame;
  return WriteCallable(
      {function.frame, function.name, function.creation.length.value,
       CallableSyntax::kArrowExpression},
      body, output, budget, constants, nullptr, captured_values);
}
}  // namespace abcd::source
