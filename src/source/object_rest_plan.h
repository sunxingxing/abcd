#ifndef ABCD_SOURCE_OBJECT_REST_PLAN_H_
#define ABCD_SOURCE_OBJECT_REST_PLAN_H_
#include "evaluation_plan.h"
#include "tree_checks.h"
namespace abcd::source {
// A native pattern consumes existing Get commits, followed by the rest copy.
// It is not permission to repeat property reads or turn an arbitrary copy into
// destructuring. Keys retain original value identities and ordered Get sites.
struct ObjectRestField {
  analysis::OpId read;
  analysis::ValueOperand key;
};
struct ObjectRestEntry {
  analysis::OpId copy;
  analysis::ValueId source;
  std::vector<ObjectRestField> fields;
};
struct ObjectRestPlan {
  std::vector<ObjectRestEntry> entries;
};
[[nodiscard]] ObjectRestPlan BuildObjectRests(const analysis::ValueProgram&,const EvaluationPlan&,TreeCheckBudget&);
[[nodiscard]] bool VerifyObjectRests(const analysis::ValueProgram&,const EvaluationPlan&,const ObjectRestPlan&,TreeCheckBudget&);
void PinObjectRests(const analysis::ValueProgram&,const EvaluationPlan&,std::span<std::uint8_t>,TreeCheckBudget&);
}
#endif
