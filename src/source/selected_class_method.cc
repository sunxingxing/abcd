#include "selected_class_method.h"

#include "abcd/source/class_method.h"
#include "abcd/source/semantic_source.h"
#include "capture_stream.h"

namespace abcd::source {
namespace {
RouteFailureClass Classify(FunctionPrepareError error) {
  switch (error) {
    case FunctionPrepareError::kResourceLimit: return RouteFailureClass::kMemoryLimit;
    case FunctionPrepareError::kWorkLimit: return RouteFailureClass::kWorkLimit;
    case FunctionPrepareError::kBody:
    case FunctionPrepareError::kFileChanged: return RouteFailureClass::kInvalidInput;
    case FunctionPrepareError::kContext:
    case FunctionPrepareError::kValues:
    case FunctionPrepareError::kControl:
    case FunctionPrepareError::kEvaluation:
    case FunctionPrepareError::kConstants:
    case FunctionPrepareError::kParameters:
    case FunctionPrepareError::kTree:
    case FunctionPrepareError::kAbi: return RouteFailureClass::kUnproved;
  }
  return RouteFailureClass::kInvariantViolation;
}
RouteFailureClass Classify(TreePrintError error) {
  switch (error) {
    case TreePrintError::kResourceLimit: return RouteFailureClass::kMemoryLimit;
    case TreePrintError::kWorkLimit: return RouteFailureClass::kWorkLimit;
    case TreePrintError::kOutputLimit:
    case TreePrintError::kWrite: return RouteFailureClass::kOutputFailure;
    case TreePrintError::kUnsupportedContext:
    case TreePrintError::kInvalidLiteral: return RouteFailureClass::kInvariantViolation;
  }
  return RouteFailureClass::kInvariantViolation;
}
}  // namespace

std::size_t WriteSelectedClassMethod(
    const ClassMethodSelection& selection, const corpus::AbcFile& file,
    const abc::Header& header, const abc::MetadataIndex& index,
    const abc::MethodLookup& lookup, const analysis::MethodRoleIndex& roles,
    const abc::ClassProperty& property, const ClassMemberTarget& target,
    std::u16string_view key, CreationExpansion& expansion,
    std::ostream& output, LocalLexicalBudget budget, const GlobalScope* scope) {
  auto* sink = dynamic_cast<CaptureBuffer*>(output.rdbuf());
  GeneratedPosition begin;
  if (!sink || !sink->Position(begin))
    throw ClassFailure{EmitError::kInvalidExpression};
  ExpansionFrame admission(expansion, index.methods[target.position.value].offset, budget);
  if (const auto error = admission.error())
    throw ClassFailure{*error == ExpansionError::kResourceLimit
        ? CreatedClassError::kResourceLimit : CreatedClassError::kRecursiveExpansion};
  // Keep disjoint preparation and printing grants; the immutable tree remains
  // live while the printer allocates its action stack and position arrays.
  constexpr std::size_t kStageShares = 2;
  const analysis::SemanticBudget preparation{budget.bytes / kStageShares,
                                            budget.work / kStageShares};
  auto prepared = VerifiedFunction::PrepareClass(file, header, index, lookup,
      roles, selection.sites, selection.site, selection.identity, preparation, scope);
  auto* function = std::get_if<VerifiedFunction>(&prepared);
  if (!function) {
    const auto& failure = std::get<FunctionPrepareFailure>(prepared);
    if (failure.values && failure.values->failure)
      throw ClassFailure{RouteRefusal{*failure.values->failure}};
    throw ClassFailure{RouteRefusal{{Classify(failure.kind),
        failure.kind == FunctionPrepareError::kContext ? ValueRouteError::kContextStage
                                                      : ValueRouteError::kTreeStage,
        failure.pc}}};
  }
  const ClassMethod method{std::get<abc::ClassMethod>(property.value), target.attributes,
      function->Frame(), property.placement, key, {}, EffectiveMethodKind::kOrdinary};
  auto printed = WriteClassMethod(method, std::move(*function), output, {begin.byte},
      budget.output, {budget.bytes - preparation.bytes, budget.work - preparation.work}, scope);
  auto* emission = std::get_if<FunctionEmission>(&printed);
  if (!emission) {
    if (const auto* error = std::get_if<FunctionEmitError>(&printed)) throw ClassFailure{*error};
    throw ClassFailure{RouteRefusal{{Classify(std::get<TreePrintError>(printed)),
                                    ValueRouteError::kPrintStage, {}}}};
  }
  GeneratedPosition end;
  if (!sink->Position(end)) throw ClassFailure{EmitError::kInvalidExpression};
  const auto bytes = emission->Positions().bytes;
  if (!sink->RetainFunction({std::make_shared<const FunctionEmission>(std::move(*emission)),
                             begin, end}))
    throw ClassFailure{EmitError::kInvalidExpression};
  return bytes;
}
}  // namespace abcd::source
