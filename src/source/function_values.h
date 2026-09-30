#ifndef ABCD_SOURCE_FUNCTION_VALUES_H_
#define ABCD_SOURCE_FUNCTION_VALUES_H_

#include "abcd/analysis/value_environment.h"
#include "function_input.h"
#include "../analysis/value_heritage.h"
#include "cell_plan.h"
#include "class_initializers.h"
#include "module_guards.h"

namespace abcd::source {
// Working state, not a source receipt. Conditional input assumptions travel
// with the result; no consumer may turn them into closed publication evidence.
struct FunctionValues {
  analysis::ValueProgram graph;
  std::optional<analysis::ValueEnvironments> environments;
  InputCaptureMode capture_mode;
  RouteInputIdentity identity;
  abc::EntityOffset method;
  abc::EntityOffset record;
  // Conditional preparation retains the checked mutable-cell index once per
  // function. Rebuild it after graph edits; never rescan a graph per capture.
  std::optional<CellPlan> cells;
  ModuleGuards modules;
  // Bound to graph.revision; recompute after edits before class-tree lowering.
  analysis::ValueHeritages classes;
  ClassInitializerCalls initializers;
};
struct FunctionValueFailure {
  SourceStage stage;
  RouteFailure cause;
  std::optional<std::uint64_t> revision;
};
// Shared by closed source preparation and conditional child preparation.
// Input/body/frame are one unchanged method. Caller keeps FunctionInput for
// parent-site binding and admits it and body alongside this additional budget.
// Does not discharge incoming cells or transitive closure writes.
[[nodiscard]] std::variant<FunctionValues, FunctionValueFailure> BuildFunctionValues(
    const FunctionInput& input, const abc::MethodBody& body,
    const abc::FrameLayout& frame, analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
