#include "../../../src/source/family_route_failure.h"

#include <cstdlib>

namespace {
using namespace abcd;
using namespace abcd::source;
void Check(bool ok) { if (!ok) std::abort(); }
void Failures() {
  for (const auto cause : {analysis::ExceptionThrowDomainError::kGuard, analysis::ExceptionThrowDomainError::kPhi,
      analysis::ExceptionThrowDomainError::kActivation, analysis::ExceptionThrowDomainError::kCoverage}) {
    analysis::ExceptionInputFailure input{analysis::ExceptionInputError::kMismatchedAnalysis,{17}};
    input.throw_domain=cause;
    const auto refused=FamilyRouteFailure(FamilyPrepareFailure{FamilyFileFailure{PhasedFileFailure{PhasedInputFailure{input}}}});
    Check(refused.causes.View().back().domain==FailureDomain<analysis::ExceptionThrowDomainError>() &&
        refused.causes.View().back().value==static_cast<unsigned>(cause));
  }

  FunctionTreeFailure census{FunctionTreeError::kSemantics};
  census.cause = TreeSemanticFailure{TreeSemanticError::kFlow};
  census.tree.emplace();
  census.tree->census = TreeCensusFailure{TreeCensusError::kOrphanExpression, {3}, {7}, analysis::OpId{11}};
  auto census_route = FamilyRouteFailure(census);
  Check(census_route.tree && census_route.tree->census == census.tree->census);
  Check(census_route.causes.View().back().domain == FailureDomain<TreeCensusError>() &&
      census_route.causes.View().back().value == static_cast<unsigned>(TreeCensusError::kOrphanExpression));
  census.placement = ForOfPlacementFailure{ForOfPlacementError::kCoverage, {}, {}, *census.tree->census};
  census_route = FamilyRouteFailure(census);
  bool placement = false;
  for (const auto& cause : census_route.causes.View())
    placement |= cause.domain == FailureDomain<ForOfPlacementError>() &&
        cause.value == static_cast<unsigned>(ForOfPlacementError::kCoverage);
  Check(placement);

  auto projected = FamilyRouteFailure(FamilyPrepareFailure{FamilyPrepareError::kResourceLimit});
  Check(projected.failure_class == RouteFailureClass::kMemoryLimit);
  FamilyTreeFailure tree{{2}, FamilyWriteFailure{FamilyWriteError::kWorkLimit, {2}, abc::CodeOffset{31}}};
  projected = FamilyRouteFailure(FamilyPrepareFailure{tree});
  Check(projected.failure_class == RouteFailureClass::kWorkLimit && projected.pc == abc::CodeOffset{31});
  FamilyFileFailure file{FunctionFileFailure{FunctionValueFailure{SourceStage::kValuesVerified,
      {RouteFailureClass::kInvariantViolation, ValueRouteError::kTransformStage, abc::CodeOffset{42}}, {1}}}};
  projected = FamilyRouteFailure(FamilyPrepareFailure{file});
  Check(projected.failure_class == RouteFailureClass::kInvariantViolation &&
      projected.reason == ValueRouteError::kTransformStage && projected.pc == abc::CodeOffset{42});
  BodyEmitFailure object{BodyEmitError::kConstant, {17}};
  object.object_error = ObjectBufferError::kResourceLimit;
  projected = FamilyRouteFailure(FamilyPrepareFailure{FamilyFileFailure{object}});
  Check(projected.failure_class == RouteFailureClass::kMemoryLimit && projected.pc == abc::CodeOffset{17});
  projected = FamilyRouteFailure(FamilyPrepareFailure{FamilyFileFailure{
      BufferedArrayError::kResourceLimit, {}, {}, abc::CodeOffset{85}}});
  Check(projected.failure_class == RouteFailureClass::kMemoryLimit && !projected.pc &&
      projected.creation_pc == abc::CodeOffset{85});
  projected = FamilyRouteFailure(FamilyPrepareFailure{FamilyFileFailure{
      abc::LiteralFailure{abc::LiteralError::kTruncated, {160}}, {}, {}, abc::CodeOffset{85}}});
  Check(!projected.pc && projected.creation_pc == abc::CodeOffset{85});
  PhasedImportFailure imported{PhasedMethodError::kImport,
      {analysis::ValueImportError::kInputMismatch, abc::CodeOffset{73}}, {91}, abc::CodeOffset{31}};
  projected = FamilyRouteFailure(FamilyPrepareFailure{FamilyFileFailure{
      PhasedFileFailure{imported}, abc::EntityOffset{91}, abc::EntityOffset{13}, abc::CodeOffset{7}}});
  Check(projected.pc == abc::CodeOffset{73} && projected.working_pc == abc::CodeOffset{31} &&
      projected.method == abc::EntityOffset{91} && projected.creation_method == abc::EntityOffset{13} &&
      projected.creation_pc == abc::CodeOffset{7});
  Check(projected.causes.View().back().domain == FailureDomain<analysis::ValueImportError>());
  imported.kind = PhasedMethodError::kWorkLimit;
  imported.cause = {analysis::ValueImportError::kWorkLimit, {}};
  imported.working_pc.reset();
  projected = FamilyRouteFailure(FamilyPrepareFailure{FamilyFileFailure{PhasedFileFailure{imported},
      abc::EntityOffset{91}, abc::EntityOffset{13}, abc::CodeOffset{7}}});
  Check(projected.failure_class == RouteFailureClass::kWorkLimit && !projected.pc && !projected.working_pc);
  ExceptionControlRegionFailure exits{ExceptionControlError::kMultipleExits, {0}, {}};
  exits.exits = {ExceptionExitEvidence{analysis::EdgeId{0}, analysis::WorkBlockId{0}, {1}, {1},
                     abc::CodeOffset{0}, abc::CodeOffset{12}},
                ExceptionExitEvidence{analysis::TryId{1}, {}, {2}, {2}}};
  exits.exception_region = ExceptionRegionEvidence{{0}, abc::CodeOffset{0}, {}, {12}, {24}, exits.exits};
  const PhasedControlFailure located{PhasedMethodError::kControl, exits, {91}, abc::CodeOffset{0}, abc::CodeOffset{12}};
  projected = FamilyRouteFailure(FamilyPrepareFailure{FamilyFileFailure{PhasedFileFailure{located}}});
  Check(projected.failure_class == RouteFailureClass::kUnproved &&
      projected.method == abc::EntityOffset{91} && projected.pc == abc::CodeOffset{0} &&
      projected.working_pc == abc::CodeOffset{12} && projected.exception_region == exits.exception_region);
  Check(projected.causes.View().back().domain == FailureDomain<ExceptionControlError>() &&
      projected.causes.View().back().value == static_cast<unsigned>(ExceptionControlError::kMultipleExits));
  for (const auto [error, category] : {
      std::pair{ExceptionControlError::kResourceLimit, RouteFailureClass::kMemoryLimit},
      std::pair{ExceptionControlError::kWorkLimit, RouteFailureClass::kWorkLimit}}) {
    const PhasedControlFailure resource{PhasedMethodError::kControl, error, {91}};
    projected = FamilyRouteFailure(FamilyPrepareFailure{FamilyFileFailure{PhasedFileFailure{resource}}});
    Check(projected.failure_class == category && !projected.exception_region);
  }
  CallPlanFailure call{CallPlanError::kReference};
  call.cause = CallReferenceError::kNotInline;
  call.reference = CallReferenceEvidence{CallReferenceError::kNotInline, {17}, analysis::OpId{4},
      EvaluationKind::kRoot, TemporaryReason::kEffectSnapshot, {}};
  call.pc = abc::CodeOffset{73}; call.working_pc = abc::CodeOffset{31};
  projected = FamilyRouteFailure(FamilyPrepareFailure{FamilyTreeFailure{{2},
      FunctionTreeFailure{FunctionTreeError::kExpression,
          ExpressionFailure{ExpressionError::kUnsupportedOperation, call}, abc::EntityOffset{91}}}});
  Check(projected.call_reference == call.reference && projected.pc == call.pc &&
      projected.working_pc == call.working_pc && projected.method == abc::EntityOffset{91});
  Check(projected.causes.View().back().domain == FailureDomain<TemporaryReason>());
  call.reference->checked = GuardedCallFailure{GuardedCallError::kCatchState, {17}, {5}, {6}, {9}};
  projected = FamilyRouteFailure(FamilyPrepareFailure{FamilyTreeFailure{{2},
      FunctionTreeFailure{FunctionTreeError::kExpression,
          ExpressionFailure{ExpressionError::kUnsupportedOperation, call}, abc::EntityOffset{91}}}});
  Check(projected.failure_class == RouteFailureClass::kUnproved && projected.call_reference == call.reference);
  Check(projected.causes.View().back().domain == FailureDomain<GuardedCallError>() &&
      projected.causes.View().back().value == static_cast<unsigned>(GuardedCallError::kCatchState));
  analysis::FunctionSiteFailure sites{abc::BodyFailure{abc::BodyError::kBranchBoundary, {19}}, {}, abc::CodeOffset{7}};
  projected = FamilyRouteFailure(sites);
  Check(projected.failure_class == RouteFailureClass::kInvalidInput && projected.pc == abc::CodeOffset{19});
  analysis::SemanticFailure semantic{analysis::SemanticError::kSsa, {29}, analysis::IdfError::kWorkLimit};
  projected = FamilyRouteFailure(FamilyPrepareFailure{FamilyFileFailure{
      FunctionFileFailure{FunctionInputFailure{semantic}}}});
  Check(projected.failure_class == RouteFailureClass::kWorkLimit && projected.pc == abc::CodeOffset{29});
  projected = FamilyRouteFailure(FamilyEmitFailure{TreePrintError::kOutputLimit});
  Check(projected.failure_class == RouteFailureClass::kOutputFailure && projected.reason == ValueRouteError::kPrintStage);
  projected = FamilyRouteFailure(FamilyEmitFailure{FamilyEmitError::kPositions});
  Check(projected.failure_class == RouteFailureClass::kInvariantViolation);
  projected = FamilyRouteFailure(FamilyPrepareFailure{FamilyEnclosureFailure{FamilyEnclosureError::kMissingContext, {0}, {}}});
  Check(projected.failure_class == RouteFailureClass::kUnproved && !projected.pc);
}
}  // namespace
int main() { Failures(); }
