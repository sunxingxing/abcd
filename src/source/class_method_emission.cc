#include "abcd/source/class_method.h"

#include <algorithm>
#include <limits>

namespace abcd::source {
namespace {
bool SameFrame(const abc::FrameLayout& left, const abc::FrameLayout& right) {
  return left.locals == right.locals && left.function == right.function &&
      left.new_target == right.new_target && left.this_value == right.this_value &&
      left.first_parameter == right.first_parameter && left.parameters == right.parameters &&
      left.registers == right.registers && left.extra_arguments == right.extra_arguments;
}
}  // namespace

FunctionEmissionResult WriteClassMethod(const ClassMethod& method,
    VerifiedFunction function, std::ostream& output, GeneratedOffset destination,
    EmitBudget output_budget, analysis::SemanticBudget scratch, const GlobalScope* scope) {
  const auto* creation = function.ClassCreation();
  if (!creation) return FunctionEmitError::kCreation;
  const auto& site = *creation;
  if (method.member.target != site.target || method.member.kind != site.kind ||
      method.placement != site.placement) return FunctionEmitError::kCreation;
  if (method.attributes.sendable) return FunctionEmitError::kSendable;
  if (method.attributes.kind != abc::FunctionKind::kNone ||
      method.effective_kind != EffectiveMethodKind::kOrdinary ||
      function.Tree().callable != CallableKind::kMethod ||
      function.Tree().strictness != Strictness::kStrict)
    return FunctionEmitError::kKind;
  if (!SameFrame(method.frame, function.Frame())) return FunctionEmitError::kFrame;
  if (method.member.length != function.Abi().length ||
      method.member.length != site.length) return FunctionEmitError::kLength;
  if (method.key != function.Name()) return FunctionEmitError::kName;
  const auto& abi = function.Abi();
  if (abi.callable != AbiObservation::kUnobserved ||
      abi.new_target != AbiObservation::kUnobserved ||
      abi.arguments != AbiObservation::kUnobserved) return FunctionEmitError::kFrame;
  switch (abi.layout) {
    case ParameterLayout::kSimple:
    case ParameterLayout::kLiteralDefaults: break;
    default: return FunctionEmitError::kFrame;
  }

  auto emitted = FunctionEmission::Write(std::move(function), output, destination,
                                         output_budget, scratch, scope);
  if (auto* result = std::get_if<FunctionEmission>(&emitted)) return std::move(*result);
  return std::get<TreePrintError>(emitted);
}

std::variant<FunctionEmission, TreePrintError> FunctionEmission::Write(
    VerifiedFunction function, std::ostream& output, GeneratedOffset destination,
    EmitBudget output_budget, analysis::SemanticBudget scratch, const GlobalScope* scope) {
  if (ValidateRouteReceipt(function.Receipt()) ||
      function.Receipt().stage != SourceStage::kTreeVerified)
    return TreePrintError::kUnsupportedContext;
  // Overflow is an output-budget failure before any wrapping addition, never
  // a wrapped generated position. The caller counts the enclosing segment.
  output_budget.bytes = std::min(output_budget.bytes,
      std::numeric_limits<std::size_t>::max() - destination.byte);
  auto printed = PrintFunctionTree(function, output, output_budget, scratch, scope);
  auto* positions = std::get_if<PrintedFunction>(&printed);
  if (!positions) return std::get<TreePrintError>(printed);
  const GeneratedRange range{destination.byte, destination.byte + positions->bytes};
  auto receipt = function.Receipt();
  receipt.stage = SourceStage::kPrinted;
  receipt.coverage = Coverage::kComplete;
  std::size_t owned = sizeof(FunctionEmission);
  const auto add = [&](std::size_t count, std::size_t width) {
    if (count > (std::numeric_limits<std::size_t>::max() - owned) / width) return false;
    owned += count * width;
    return true;
  };
  if (!add(function.AllocatedBytes(), 1) ||
      !add(positions->parameters.capacity(), sizeof(GeneratedRange)) ||
      !add(positions->expressions.capacity(), sizeof(GeneratedRange)) ||
      !add(positions->properties.capacity(), sizeof(GeneratedRange)) ||
      !add(positions->statements.capacity(), sizeof(GeneratedRange)) ||
      !add(positions->anchors.capacity(), sizeof(std::size_t)) ||
      !add(positions->expression_extents.capacity(), sizeof(GeneratedExtent)) ||
      !add(positions->property_extents.capacity(), sizeof(GeneratedExtent)) ||
      !add(positions->statement_extents.capacity(), sizeof(GeneratedExtent)))
    return TreePrintError::kResourceLimit;
  return FunctionEmission{std::move(function), std::move(*positions), range, receipt, owned};
}
}  // namespace abcd::source
