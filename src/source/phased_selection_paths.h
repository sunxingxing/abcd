#ifndef ABCD_SOURCE_PHASED_SELECTION_PATHS_H_
#define ABCD_SOURCE_PHASED_SELECTION_PATHS_H_
#include "logical_regions.h"
namespace abcd::source {
// Index closed nested paths from original normal CFG/phi edges. Partial children
// retain their shared phi identity; executable placement needs operand/scope proof.
std::vector<SelectionRegion> NestedPhaseSelections(const analysis::ValueProgram&,
    const EvaluationPlan&, TreeCheckBudget&);
}
#endif
