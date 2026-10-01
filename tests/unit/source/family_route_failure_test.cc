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
  Check(projected.failure_class == RouteFailureClass::kMemoryLimit && projected.pc == abc::CodeOffset{85});
  projected = FamilyRouteFailure(FamilyPrepareFailure{FamilyFileFailure{
      abc::LiteralFailure{abc::LiteralError::kTruncated, {160}}, {}, {}, abc::CodeOffset{85}}});
  Check(projected.pc == abc::CodeOffset{85});  // Entity offsets must not become bytecode PCs.
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
