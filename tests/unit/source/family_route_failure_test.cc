#include "../../../src/source/family_route_failure.h"

#include <cstdlib>

namespace {
using namespace abcd;
using namespace abcd::source;
void Check(bool ok) { if (!ok) std::abort(); }
void Failures() {
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
