#ifndef ABCD_SOURCE_JOINT_SELECTION_USE_H_
#define ABCD_SOURCE_JOINT_SELECTION_USE_H_
#include "abcd/analysis/value_ir.h"
namespace abcd::source {
class TreeCheckBudget;
struct EvaluationPlan;
struct SelectionRegion;
// One evaluated phi, its pure decision occurrences, and its unchanged keep
// edge. Discovery is a proposal; region truth and the call interval seal it.
struct JointSelectionUse {
  analysis::ValueId value;
  analysis::UseId keep;
  analysis::OpId call;
  std::vector<analysis::UseId> predicates;
  bool operator==(const JointSelectionUse&) const = default;
};
std::vector<JointSelectionUse> FindJointSelectionUses(const analysis::ValueProgram&,
    const EvaluationPlan&, TreeCheckBudget&);
void VerifyJointSelectionUses(const analysis::ValueProgram&, const EvaluationPlan&,
    std::span<const JointSelectionUse>, TreeCheckBudget&);
}
#endif
