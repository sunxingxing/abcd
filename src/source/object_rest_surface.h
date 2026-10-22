#ifndef ABCD_SOURCE_OBJECT_REST_SURFACE_H_
#define ABCD_SOURCE_OBJECT_REST_SURFACE_H_
#include "tree_checks.h"
namespace abcd::source {
struct TreeFlow;
struct EvaluationPlan;
struct OriginalFlow;
struct ObjectRestSurfaceField {
  StmtId statement; ExprId expression; SymbolId target;
  ExprId base; MemberKey key;
  bool operator==(const ObjectRestSurfaceField&)const=default;
};
struct ObjectRestSurface {
  StmtId statement; ExprId expression; SymbolId target;
  std::vector<ObjectRestSurfaceField> fields;
  ExprId source; ArgumentRange keys;
};
void RetainObjectRests(const analysis::ValueProgram&,const EvaluationPlan&,const FunctionTree&,const TreeFlow&,OriginalFlow&,TreeCheckBudget&);
// Recheck actual syntax against retained original result/operand commits. Every
// recovery projection must preserve these occurrences until a pattern-aware
// recovery owner replaces them; no arbitrary copy carrier is printable.
void VerifyObjectRestSurface(const FunctionTree&,const OriginalFlow&,TreeCheckBudget&);
}
#endif
