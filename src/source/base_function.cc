#include "abcd/source/base_function.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <limits>
#include <memory>
#include <ostream>
#include <streambuf>
#include <string>
#include <type_traits>
#include <vector>

#include "abcd/abc/register_access.h"
#include "abcd/analysis/first_for_in.h"
#include "abcd/analysis/derived_constructor.h"
#include "abcd/analysis/module_load.h"
#include "abcd/analysis/rest_arguments.h"
#include "abcd/source/base_constructor.h"
#include "abcd/source/class_method.h"
#include "abcd/source/string_literal.h"
#include "abcd/source/semantic_body.h"
#include "abcd/source/async_completion.h"
#include "capture_stream.h"
#include "branch_value_proof.h"
#include "branch_source_facts.h"
#include "constructor_fields.h"
#include "function_template.h"
#include "staged_branch_call.h"
#include "structured_catch.h"

namespace abcd::source {
namespace {
enum class CallableSyntax {
  kFunctionExpression,
  kAsyncFunctionExpression,
  kArrowExpression,
  kAsyncArrowExpression,
  kBaseConstructor,
  kRangeDerivedConstructor,
  kClassMethod,
  kAsyncClassMethod,
  kClassGetter,
  kClassSetter
};
enum class ParameterForm { kFixed, kRest, kNeutralDefault };
struct CallableShape {
  abc::FrameLayout frame;
  std::string_view name;
  std::uint16_t length;
  CallableSyntax syntax;
  std::u16string_view key{};
  abc::ClassPlacement placement = abc::ClassPlacement::kPrototype;
  std::u16string_view arrow_name{};
  ParameterForm parameters = ParameterForm::kFixed;
};
bool IsArrowSyntax(CallableSyntax syntax) {
  return syntax == CallableSyntax::kArrowExpression ||
         syntax == CallableSyntax::kAsyncArrowExpression;
}
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

std::optional<FunctionEmitError> AccessorFunctionSlot(
    const abc::MethodBody& body, const abc::FrameLayout& frame,
    std::size_t bytes) {
  if (!frame.function) return {};
  const auto count = static_cast<std::size_t>(frame.registers) + 1;
  if (count > bytes / sizeof(std::int64_t))
    return FunctionEmitError::kResourceLimit;
  try {
    // Difference counts avoid O(instructions * range width) for call operands.
    std::vector<std::int64_t> reads(count);
    for (const auto& site : body.instructions) {
      const auto accesses = abc::GetRegisterAccesses(
          site.decoded, frame.registers);
      const auto* known = std::get_if<abc::RegisterAccesses>(&accesses);
      if (!known) return FunctionEmitError::kFrame;
      for (std::size_t i = 0; i < known->count; ++i) {
        const auto& window = known->windows[i];
        if (window.access == abc::OperandAccess::kWrite) continue;
        ++reads[window.first.value];
        --reads[window.first.value + window.count];
      }
    }
    for (std::size_t i = 1; i < reads.size(); ++i)
      reads[i] += reads[i - 1];
    const auto self = frame.function->value;
    for (const auto& site : body.instructions) {
      const auto accesses = abc::GetRegisterAccesses(
          site.decoded, frame.registers);
      const auto& known = std::get<abc::RegisterAccesses>(accesses);
      bool reads_self = false;
      for (std::size_t i = 0; i < known.count; ++i) {
        const auto& window = known.windows[i];
        reads_self |= window.access != abc::OperandAccess::kWrite &&
                      self >= window.first.value &&
                      self - window.first.value < window.count;
      }
      if (!reads_self) continue;
      const auto lifted = analysis::LiftScalarInstruction(
          site.decoded, frame.registers);
      const auto* copy = std::get_if<analysis::ScalarAssignment>(&lifted);
      if (!copy || (site.decoded.opcode != abc::Opcode::kMOV_V4_V4 &&
                    site.decoded.opcode != abc::Opcode::kMOV_V8_V8))
        return FunctionEmitError::kFrame;
      const auto* source = std::get_if<analysis::ScalarValue>(
          &copy->expression);
      const auto* origin = source
                               ? std::get_if<abc::RegisterId>(source)
                               : nullptr;
      const auto* destination =
          std::get_if<abc::RegisterId>(&copy->destination);
      if (!origin || origin->value != self || !destination ||
          reads[destination->value] != 0)
        return FunctionEmitError::kFrame;
    }
    return {};
  } catch (const std::bad_alloc&) {
    return FunctionEmitError::kResourceLimit;
  } catch (const std::length_error&) {
    return FunctionEmitError::kResourceLimit;
  }
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
      const bool non_simple_parameters =
          function_.parameters != ParameterForm::kFixed;
      const bool strict_wrapper = non_simple_parameters &&
          (function_.syntax == CallableSyntax::kFunctionExpression ||
           function_.syntax == CallableSyntax::kAsyncFunctionExpression);
      if (strict_wrapper) Text("(() => {\"use strict\"; return ");
      if (function_.syntax == CallableSyntax::kClassMethod ||
          function_.syntax == CallableSyntax::kAsyncClassMethod ||
          function_.syntax == CallableSyntax::kClassGetter ||
          function_.syntax == CallableSyntax::kClassSetter) {
        if (function_.placement == abc::ClassPlacement::kStatic)
          Text("static ");
        Text(function_.syntax == CallableSyntax::kAsyncClassMethod
                 ? "async ["
             : function_.syntax == CallableSyntax::kClassGetter
                 ? "get ["
             : function_.syntax == CallableSyntax::kClassSetter ? "set ["
                                                                : "[");
        if (error_) return *error_;
        const auto key = WriteStringLiteral(function_.key, output_,
                                            {budget_.output.bytes - written_});
        if (const auto* error = std::get_if<EmitError>(&key)) return *error;
        written_ += std::get<std::size_t>(key);
        Text("](");
      } else if (function_.syntax == CallableSyntax::kBaseConstructor ||
                 function_.syntax == CallableSyntax::kRangeDerivedConstructor) {
        Text("constructor(");
      } else if (IsArrowSyntax(function_.syntax)) {
        // A computed data-property name applies SetFunctionName without a
        // mutable intrinsic; arrow IIFEs retain lexical this/new.target.
        Text(non_simple_parameters
                 ? "(() => {\n\"use strict\";\nconst $arrow = ({["
                 : "(() => {\nconst $arrow = ({[");
        ArrowName();
        Text(function_.syntax == CallableSyntax::kAsyncArrowExpression
                 ? "]: async (" : "]: (");
      } else {
        Text(function_.syntax == CallableSyntax::kAsyncFunctionExpression
                 ? "(async function " : "(function ");
        Text(function_.name);
        Text("(");
      }
      const auto& frame = function_.frame;
      for (std::uint32_t i = 0; i < frame.parameters && !error_; ++i) {
        if (i != 0) Text(", ");
        if (function_.parameters == ParameterForm::kRest &&
            i + 1 == frame.parameters)
          Text("...");
        Parameter(i);
        if (function_.parameters == ParameterForm::kNeutralDefault &&
            i == function_.length)
          Text(" = void 0");
      }
      Text(IsArrowSyntax(function_.syntax)
               ? non_simple_parameters ? ") => {\nlet $acc;\n"
                                : ") => {\n\"use strict\";\nlet $acc;\n"
               : non_simple_parameters ? ") {\nlet $acc;\n"
                                : ") {\n\"use strict\";\nlet $acc;\n");
      // Each VM register is independently writable. In particular, a function
      // slot must not alias the immutable named-expression self binding.
      for (std::uint32_t i = 0; i < frame.registers && !error_; ++i) {
        Text("let ");
        Text(names_.registers[i]);
        const abc::RegisterId slot{i};
        if (frame.function == slot) {
          Text(" = ");
          Text(function_.syntax == CallableSyntax::kClassGetter ||
                       function_.syntax == CallableSyntax::kClassSetter
                   ? "void 0"
                   : IsArrowSyntax(function_.syntax)
                         ? "$arrow"
                         : function_.name);
        } else if (frame.new_target == slot) {
          if (!IsArrowSyntax(function_.syntax))
            Text(" = new.target");
        } else if (frame.this_value == slot) {
          if (function_.syntax == CallableSyntax::kRangeDerivedConstructor)
            Text(" = void 0");
          else if (!IsArrowSyntax(function_.syntax))
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
      if (constants_) {
        scoped_constants.emplace(*constants_);
        if (methods_ || !scoped_constants->expansion)
          scoped_constants->leaf_methods = methods_;
        if (scoped_constants->global_scope &&
            (function_.syntax == CallableSyntax::kFunctionExpression ||
             function_.syntax == CallableSyntax::kAsyncFunctionExpression))
          scoped_constants->global_scope->self_binding = function_.name;
        body_constants = &*scoped_constants;
      }
      FunctionEmitResult result{std::size_t{0}};
      std::optional<PrintedSource> semantic_body;
      std::optional<PrintedTemplate> semantic_template;
      bool semantic_resource_limit = false;
      const auto resource_failure = [](const auto& prepared) {
        return std::visit([](const auto& value) {
          using T = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<T, PrintedSource> ||
                        std::is_same_v<T, PrintedTemplate>) {
            return false;
          } else {
            if (value.kind == decltype(value.kind)::kResourceLimit) return true;
            if constexpr (std::is_same_v<T, analysis::SemanticFailure>) {
              return std::visit([](const auto& cause) {
                using Cause = std::decay_t<decltype(cause)>;
                if constexpr (std::is_same_v<Cause, analysis::RegisterUseFailure>)
                  return cause.kind == analysis::RegisterUseError::kWorkLimit;
                else if constexpr (requires { Cause::kWorkLimit; })
                  return cause == Cause::kWorkLimit;
                else
                  return false;
              }, value.ssa_cause);
            }
            return false;
          }
        }, prepared);
      };
      const bool ordinary_body =
          function_.syntax == CallableSyntax::kFunctionExpression ||
          function_.syntax == CallableSyntax::kAsyncFunctionExpression ||
          function_.syntax == CallableSyntax::kArrowExpression ||
          function_.syntax == CallableSyntax::kAsyncArrowExpression ||
          function_.syntax == CallableSyntax::kBaseConstructor ||
          function_.syntax == CallableSyntax::kRangeDerivedConstructor ||
          function_.syntax == CallableSyntax::kClassMethod ||
          function_.syntax == CallableSyntax::kAsyncClassMethod ||
          function_.syntax == CallableSyntax::kClassGetter ||
          function_.syntax == CallableSyntax::kClassSetter;
      // The wrapper already proves neutral-default .length adaptation. The
      // original default-assignment bytecode stays in the generic body; do not
      // route non-simple formals back to a dedicated body writer.
      if (ordinary_body && body_constants) {
        if (captured_values_ == analysis::CapturedValues::kLanguage) {
          auto prepared = BuildSemanticTemplate(
              body, frame, names_, *body_constants,
              {body_budget.bytes, body_budget.work}, body_budget.output.bytes);
          semantic_resource_limit = resource_failure(prepared);
          if (auto* source = std::get_if<PrintedTemplate>(&prepared))
            semantic_template = std::move(*source);
        } else {
          auto prepared = BuildSemanticBody(
              body, frame, names_, *body_constants,
              {body_budget.bytes, body_budget.work}, body_budget.output.bytes);
          semantic_resource_limit = resource_failure(prepared);
          if (auto* source = std::get_if<PrintedSource>(&prepared))
            semantic_body = std::move(*source);
        }
      }
      // A recovered await's source semantics exist only in the generic IR.
      // Legacy writers must never reinterpret the retained VM await opcode.
      if (body_constants && !body_constants->awaits.empty() &&
          !semantic_body && !semantic_template)
        return semantic_resource_limit ? FunctionEmitError::kResourceLimit
                                       : FunctionEmitError::kKind;
      const auto first_for_in =
          body_constants &&
                  body_constants->header.profile == abc::ProfileId::kDynamic1301 &&
                  captured_values_ == analysis::CapturedValues::kReject
              ? analysis::MatchFirstForIn(body, function_.frame)
              : std::nullopt;
      if (semantic_body) {
        // The owning writer retains proven callable syntax/ABI. The generic
        // path emits no wrapper and cannot change a method into a function.
        // Its PC spans remain body-relative; record-wide relocation is separate.
        Text(semantic_body->text);
        if (error_) return *error_;
      } else if (semantic_template) {
        // Preserve every capture token for the existing parent rebase/value
        // proof. Template bytes and coordinates are not standalone source.
        auto* captures = dynamic_cast<CaptureBuffer*>(output_.rdbuf());
        if (!captures) return EmitError::kWrite;
        std::size_t begin = 0;
        for (const auto& capture : semantic_template->captures) {
          const auto end = capture.position.byte;
          if (end < begin || end > semantic_template->text.size())
            return EmitError::kWrite;
          Text(std::string_view(semantic_template->text).substr(begin, end - begin));
          if (error_) return *error_;
          if (!captures->Capture(capture.cell)) return EmitError::kWrite;
          begin = end;
        }
        Text(std::string_view(semantic_template->text).substr(begin));
        if (error_) return *error_;
      } else if (first_for_in) {
        // The v7 frontend's acquisition catch only rethrows; the complete
        // body proof permits native for-in without exposing VM iterator state.
        Text("for (const $forInKey in ");
        Text(names_.registers[first_for_in->source.value]);
        Text(") {\nreturn $forInKey;\n}\nreturn;\n");
        if (error_) return *error_;
      } else if (!body.tries.empty() || !body.handlers.empty()) {
        result = WriteStructuredCatch(body, names_, output_, body_budget,
                                      body_constants);
      } else {
        const auto lexical =
            captured_values_ == analysis::CapturedValues::kLanguage
                ? WriteLocalLexicalTemplate(body, names_, output_, body_budget,
                                            body_constants)
                : WriteLocalLexicalBody(body, names_, output_, body_budget,
                                        body_constants, methods_);
        result = std::visit(
            [](auto value) -> FunctionEmitResult { return value; }, lexical);
        if (const auto* failure =
                std::get_if<analysis::LinearLexicalFailure>(&lexical);
            failure && failure->kind == analysis::LinearLexicalError::kControlFlow) {
          AcyclicBudget ordered_budget;
          ordered_budget.bytes = body_budget.bytes;
          ordered_budget.work = body_budget.work;
          ordered_budget.output = body_budget.output;
          if (captured_values_ == analysis::CapturedValues::kReject) {
            // Module reads in branch bodies need the same guarded-load facts
            // as captured bodies, but must not admit any captured cell here.
            const bool has_module_load = std::any_of(
                body.instructions.begin(), body.instructions.end(),
                [](const abc::BodyInstruction& site) {
                  return std::holds_alternative<analysis::ModuleLoad>(
                      analysis::LiftModuleLoad(site.decoded));
                });
            if (has_module_load) {
              const auto prepared = BuildCapturedBranchFacts(
                  body, names_.registers.size(), body_constants, body_budget);
              if (const auto* error = std::get_if<BodyEmitFailure>(&prepared))
                return *error;
              const auto& facts = std::get<BranchSourceFacts>(prepared);
              if (!facts.captures.empty())
                return BodyEmitFailure{
                    BodyEmitError::kUnsupportedInstruction, {}, {}, {}};
              if (facts.AllocatedBytes() > body_budget.bytes)
                return FunctionEmitError::kResourceLimit;
              auto proof_budget = body_budget;
              proof_budget.bytes -= facts.AllocatedBytes();
              if (const auto unsafe = ProveBranchValues(
                      body, names_.registers.size(), proof_budget,
                      facts.value_facts))
                return *unsafe;
              result = std::visit(
                  [](auto value) -> FunctionEmitResult { return value; },
                  WriteCapturedBranchBody(body, names_, output_,
                                          ordered_budget, body_constants,
                                          facts));
            } else {
              if (const auto unsafe = ProveBranchValues(
                      body, names_.registers.size(), body_budget))
                return *unsafe;
              result = std::visit(
                  [](auto value) -> FunctionEmitResult { return value; },
                  WriteBlockLoopBody(body, names_, output_, ordered_budget,
                                     body_constants));
            }
          } else {
            const auto prepared = BuildCapturedBranchFacts(
                body, names_.registers.size(), body_constants, body_budget);
            if (const auto* error = std::get_if<BodyEmitFailure>(&prepared))
              return *error;
            const auto& facts = std::get<BranchSourceFacts>(prepared);
            if (facts.AllocatedBytes() > body_budget.bytes)
              return FunctionEmitError::kResourceLimit;
            auto proof_budget = body_budget;
            proof_budget.bytes -= facts.AllocatedBytes();
            if (const auto unsafe = ProveBranchValues(
                    body, names_.registers.size(), proof_budget,
                    facts.value_facts))
              return *unsafe;
            const auto staged = analysis::MatchTailNamedInvocation(
                body, names_.registers.size());
            auto ordinary = WriteCapturedBranchBody(
                body, names_, output_, ordered_budget, body_constants, facts);
            if (staged) {
              const auto* unsupported =
                  std::get_if<BodyEmitFailure>(&ordinary);
              if (unsupported &&
                  unsupported->kind ==
                      BodyEmitError::kUnsupportedInstruction)
                ordinary = WriteStagedBranchCall(
                    body, *staged, names_, output_, body_budget,
                    body_constants, facts);
            }
            result = std::visit(
                [](auto value) -> FunctionEmitResult { return value; },
                ordinary);
          }
        }
      }
      if (const auto* bytes = std::get_if<std::size_t>(&result))
        written_ += *bytes;
      else
        return result;
      if (IsArrowSyntax(function_.syntax)) {
        Text("}})[");
        ArrowName();
        Text("];\nreturn $arrow;\n})()");
      } else {
        Text(function_.syntax == CallableSyntax::kFunctionExpression ||
                     function_.syntax == CallableSyntax::kAsyncFunctionExpression
                 ? "})" : "}");
      }
      if (strict_wrapper) Text(";})()");
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
bool IsResourceFailure(const FunctionEmitResult& result) {
  return std::visit([](const auto& value) {
    using T = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<T, FunctionEmitError>)
      return value == FunctionEmitError::kResourceLimit;
    else if constexpr (std::is_same_v<T, BodyEmitFailure>)
      return value.kind == BodyEmitError::kResourceLimit;
    else if constexpr (std::is_same_v<T, analysis::FlowFailure>)
      return value.kind == analysis::FlowError::kResourceLimit;
    else if constexpr (std::is_same_v<T, analysis::EnvironmentFlowFailure>)
      return value.kind == analysis::EnvironmentFlowError::kResourceLimit;
    else if constexpr (std::is_same_v<T, analysis::LinearLexicalFailure>)
      return value.kind == analysis::LinearLexicalError::kResourceLimit;
    else if constexpr (std::is_same_v<T, analysis::AccumulatorFailure>)
      return value.kind == analysis::AccumulatorError::kResourceLimit;
    else if constexpr (std::is_same_v<T, AcyclicError>)
      return value == AcyclicError::kResourceLimit;
    else
      return false;
  }, result);
}
FunctionEmitResult WriteCallable(const CallableShape& function,
                                 const abc::MethodBody& body,
                                 std::ostream& output,
                                 LocalLexicalBudget budget,
                                 const MethodConstants* constants,
                                 const abc::MethodLookup* methods,
                                 analysis::CapturedValues captured_values) {
  if (!CanonicalFrame(function.frame)) return FunctionEmitError::kFrame;
  if (function.syntax == CallableSyntax::kRangeDerivedConstructor) {
    const auto proof = analysis::ProveRangeDerivedConstructor(
        body, function.frame, {budget.bytes, budget.work});
    if (const auto* error =
            std::get_if<analysis::DerivedConstructorError>(&proof))
      return *error == analysis::DerivedConstructorError::kResourceLimit
                 ? FunctionEmitError::kResourceLimit
                 : FunctionEmitError::kKind;
  }
  const bool length_mismatch = function.length != function.frame.parameters;
  if (function.length > function.frame.parameters)
    return FunctionEmitError::kLength;
  const auto rest_prologue =
      length_mismatch ? analysis::ProveRestPrologue(
                            body, function.frame, function.length)
                      : std::nullopt;
  const bool rest = rest_prologue.has_value();
  const bool neutral_default = length_mismatch && !rest;
  if (rest && (function.syntax == CallableSyntax::kClassGetter ||
               function.syntax == CallableSyntax::kClassSetter))
    return FunctionEmitError::kLength;
  // Historical 12.0.2 accepts only the fully proved rest prologue. Its
  // neutral-default encoding is not yet admitted as source semantics.
  // A null constants pointer retains the existing pinned-v7 caller assertion.
  if (length_mismatch && constants &&
      !(constants->header.profile == abc::ProfileId::kDynamic1202 &&
        (rest || function.syntax ==
                     CallableSyntax::kRangeDerivedConstructor)) &&
      constants->header.profile != abc::ProfileId::kDynamic1206 &&
      constants->header.profile != abc::ProfileId::kDynamic1301)
    return FunctionEmitError::kLength;
  // A neutral default formal changes only JS .length; the original bytecode
  // body still performs every actual default assignment. Reject ambiguous
  // rest instructions rather than hiding a malformed rest prologue.
  if (neutral_default) {
    if (function.syntax == CallableSyntax::kClassGetter ||
        function.syntax == CallableSyntax::kClassSetter)
      return FunctionEmitError::kLength;
    for (const auto& site : body.instructions)
      if (site.decoded.opcode == abc::Opcode::kCOPYRESTARGS_IMM8 ||
          site.decoded.opcode ==
              abc::Opcode::kWIDE_COPYRESTARGS_PREF_IMM16)
        return FunctionEmitError::kLength;
  }
  // Reuse the expression emitter's strict-safe binding validation. '$' is our
  // private source namespace; never let a supplied function name shadow it.
  CaptureDiscard buffer;
  std::ostream discard(&buffer);
  if (!IsArrowSyntax(function.syntax)) {
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
    if (IsArrowSyntax(function.syntax)) {
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
    shape.parameters = rest ? ParameterForm::kRest
                            : neutral_default ? ParameterForm::kNeutralDefault
                                              : ParameterForm::kFixed;
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
    const std::string_view current_function = [&]() -> std::string_view {
      switch (function.syntax) {
        case CallableSyntax::kFunctionExpression:
        case CallableSyntax::kAsyncFunctionExpression:
        case CallableSyntax::kBaseConstructor:
        case CallableSyntax::kRangeDerivedConstructor:
        case CallableSyntax::kClassMethod:
        case CallableSyntax::kAsyncClassMethod:
          return function.name;
        case CallableSyntax::kArrowExpression:
        case CallableSyntax::kAsyncArrowExpression:
          return "$arrow";
        case CallableSyntax::kClassGetter:
        case CallableSyntax::kClassSetter:
          return {};
      }
      return {};
    }();
    RegisterName rest_storage{};
    std::string_view rest_name;
    if (rest) {
      if (constants && !constants->source_parameters.empty()) {
        rest_name = constants->source_parameters.back();
      } else {
        rest_storage[0] = '$';
        rest_storage[1] = 'p';
        const auto end = std::to_chars(
            rest_storage.data() + 2,
            rest_storage.data() + rest_storage.size(),
            function.frame.parameters - 1).ptr;
        rest_name = {rest_storage.data(),
                     static_cast<std::size_t>(end - rest_storage.data())};
      }
    }
    auto direct_writes =
        function.syntax == CallableSyntax::kBaseConstructor
            ? ProveConstructorThisWrites(body, function.frame, budget.bytes)
            : ConstructorThisWrites{};
    if (direct_writes.AllocatedBytes() > budget.bytes)
      return FunctionEmitError::kResourceLimit;
    const auto ordinary_budget = budget;
    budget.bytes -= direct_writes.AllocatedBytes();
    ScalarNames bindings{
        "$acc", {names.get(), count}, current_function,
        rest ? std::optional<ScalarNames::RestBinding>{
                   ScalarNames::RestBinding{function.length, rest_name}}
             : std::nullopt,
        direct_writes.sites,
        function.syntax == CallableSyntax::kRangeDerivedConstructor};
    auto checked = FunctionWriter(shape, bindings, discard, budget,
                                  constants, methods, captured_values)
                       .Write(body);
    if (!direct_writes.sites.empty() && IsResourceFailure(checked)) {
      bindings.constructor_this_writes = {};
      std::vector<abc::CodeOffset>().swap(direct_writes.sites);
      budget = ordinary_budget;
      checked = FunctionWriter(shape, bindings, discard, budget, constants,
                               methods, captured_values)
                    .Write(body);
    }
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
  if (syntax == CallableSyntax::kAsyncFunctionExpression) {
    std::vector<abc::CodeOffset> origins;
    std::vector<analysis::AwaitSite> awaits;
    const auto checked = ProveAsyncCompletion(body, function.frame, budget.bytes,
        &origins, constants ? &awaits : nullptr,
        constants ? constants->header.profile : abc::ProfileId::kUnknown);
    if (const auto* error = std::get_if<FunctionEmitError>(&checked)) return *error;
    const auto& normalized = std::get<abc::MethodBody>(checked);
    budget.bytes -= normalized.instructions.capacity() * sizeof(abc::BodyInstruction) +
                    origins.capacity() * sizeof(abc::CodeOffset) +
                    awaits.capacity() * sizeof(analysis::AwaitSite);
    auto scoped = constants ? std::optional<MethodConstants>(*constants) : std::nullopt;
    if (scoped) {
      scoped->source_pcs = origins;
      scoped->awaits = awaits;
    }
    return WriteCallable(
        {function.frame, function.name, function.creation.length.value, syntax},
        normalized, output, budget, scoped ? &*scoped : nullptr, methods,
        captured_values);
  }
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
  if (method.attributes.kind != abc::FunctionKind::kNone &&
      method.attributes.kind != abc::FunctionKind::kAsync)
    return FunctionEmitError::kKind;
  if (method.attributes.kind == abc::FunctionKind::kAsync &&
      method.member.kind != abc::ClassMethodKind::kOrdinary)
    return FunctionEmitError::kKind;
  CallableSyntax syntax;
  switch (method.member.kind) {
    case abc::ClassMethodKind::kOrdinary:
      if (method.effective_kind != EffectiveMethodKind::kOrdinary)
        return FunctionEmitError::kKind;
      syntax = method.attributes.kind == abc::FunctionKind::kAsync
                   ? CallableSyntax::kAsyncClassMethod
                   : CallableSyntax::kClassMethod;
      break;
    case abc::ClassMethodKind::kGetter:
      if (method.effective_kind != EffectiveMethodKind::kGetter)
        return FunctionEmitError::kKind;
      syntax = CallableSyntax::kClassGetter;
      break;
    case abc::ClassMethodKind::kSetter:
      if (method.effective_kind != EffectiveMethodKind::kSetter)
        return FunctionEmitError::kKind;
      syntax = CallableSyntax::kClassSetter;
      break;
    case abc::ClassMethodKind::kGenerator:
    case abc::ClassMethodKind::kAsyncGenerator:
      return FunctionEmitError::kKind;
    default:
      return FunctionEmitError::kKind;
  }
  if (syntax == CallableSyntax::kClassGetter ||
      syntax == CallableSyntax::kClassSetter)
    if (const auto failure = AccessorFunctionSlot(
            body, method.frame, budget.bytes))
      return *failure;
  if (syntax == CallableSyntax::kAsyncClassMethod) {
    std::vector<abc::CodeOffset> origins;
    std::vector<analysis::AwaitSite> awaits;
    const auto checked = ProveAsyncCompletion(body, method.frame, budget.bytes,
        &origins, constants ? &awaits : nullptr,
        constants ? constants->header.profile : abc::ProfileId::kUnknown);
    if (const auto* error = std::get_if<FunctionEmitError>(&checked))
      return *error;
    const auto& normalized = std::get<abc::MethodBody>(checked);
    budget.bytes -= normalized.instructions.capacity() *
                    sizeof(abc::BodyInstruction) +
                    origins.capacity() * sizeof(abc::CodeOffset) +
                    awaits.capacity() * sizeof(analysis::AwaitSite);
    auto scoped = constants ? std::optional<MethodConstants>(*constants) : std::nullopt;
    if (scoped) {
      scoped->source_pcs = origins;
      scoped->awaits = awaits;
    }
    return WriteCallable(
        {method.frame, method.self_binding, method.member.length,
         syntax, method.key, method.placement},
        normalized, output, budget, scoped ? &*scoped : nullptr, methods,
        analysis::CapturedValues::kReject);
  }
  return WriteCallable(
      {method.frame, method.self_binding, method.member.length,
       syntax, method.key, method.placement},
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
FunctionEmitResult WriteRangeDerivedConstructor(
    const RangeDerivedConstructor& constructor, const abc::MethodBody& body,
    std::ostream& output, LocalLexicalBudget budget,
    const MethodConstants* constants, const abc::MethodLookup* methods) {
  if (constructor.attributes.sendable) return FunctionEmitError::kSendable;
  if (constants && constants->header.profile != abc::ProfileId::kDynamic1202 &&
      constants->header.profile != abc::ProfileId::kDynamic1206 &&
      constants->header.profile != abc::ProfileId::kDynamic1301)
    return FunctionEmitError::kKind;
  return WriteCallable(
      {constructor.frame, constructor.class_binding,
       constructor.creation.length,
       CallableSyntax::kRangeDerivedConstructor},
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
  if (function.attributes.kind != abc::FunctionKind::kArrow &&
      function.attributes.kind != abc::FunctionKind::kAsyncArrow)
    return FunctionEmitError::kKind;
  // Pinned interpreter Execute copies call-info this/newTarget into ABI slots.
  // A native arrow cannot expose those dynamic slots; lexical accesses use
  // separate environment operations and are handled by the body writer.
  if (ObservesArrowCallContext(body, function.frame))
    return FunctionEmitError::kFrame;
  if (function.attributes.kind == abc::FunctionKind::kAsyncArrow) {
    // Recover the private completion wrapper and any supported await regions
    // before the generic IR receives source-level suspension semantics.
    std::vector<abc::CodeOffset> origins;
    std::vector<analysis::AwaitSite> awaits;
    const auto checked = ProveAsyncCompletion(body, function.frame, budget.bytes,
        &origins, constants ? &awaits : nullptr,
        constants ? constants->header.profile : abc::ProfileId::kUnknown);
    if (const auto* error = std::get_if<FunctionEmitError>(&checked))
      return *error;
    const auto& normalized = std::get<abc::MethodBody>(checked);
    budget.bytes -= normalized.instructions.capacity() *
                    sizeof(abc::BodyInstruction) +
                    origins.capacity() * sizeof(abc::CodeOffset) +
                    awaits.capacity() * sizeof(analysis::AwaitSite);
    auto scoped = constants ? std::optional<MethodConstants>(*constants) : std::nullopt;
    if (scoped) {
      scoped->source_pcs = origins;
      scoped->awaits = awaits;
    }
    return WriteCallable(
        {function.frame, function.name, function.creation.length.value,
         CallableSyntax::kAsyncArrowExpression},
        normalized, output, budget, scoped ? &*scoped : nullptr, nullptr, captured_values);
  }
  return WriteCallable(
      {function.frame, function.name, function.creation.length.value,
       CallableSyntax::kArrowExpression},
      body, output, budget, constants, nullptr, captured_values);
}
}  // namespace abcd::source
