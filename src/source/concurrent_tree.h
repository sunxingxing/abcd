#ifndef ABCD_SOURCE_CONCURRENT_TREE_H_
#define ABCD_SOURCE_CONCURRENT_TREE_H_
#include "tree_flow.h"
namespace abcd::source {
void VerifyConcurrentTree(const analysis::ValueProgram&,const EvaluationPlan&,const FunctionTree&,TreeFlow&,TreeCheckBudget&);
void VerifyOriginalConcurrent(const FunctionTree&,const OriginalFlow&,TreeCheckBudget&);
}
#endif
