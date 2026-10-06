#ifndef ABCD_SOURCE_PARAMETER_CELL_NAMES_H_
#define ABCD_SOURCE_PARAMETER_CELL_NAMES_H_
#include "function_syntax.h"
namespace abcd::source {
struct ParameterCellNameCost { std::size_t bytes; std::uint64_t work; };
enum class ParameterCellNameError { kInput, kResourceLimit, kWorkLimit };
// Presentation only: a metadata-named cell receiving the exact entry value may
// suggest an unnamed formal. This does not merge storage, prove mutability or
// authorize motion. Existing formal evidence and actual scope checks prevail.
[[nodiscard]] std::variant<ParameterCellNameCost, ParameterCellNameError> NameCellParameters(
    const analysis::ValueProgram&, const CellPlan&, FunctionSyntax&, analysis::SemanticBudget = {});
}
#endif
