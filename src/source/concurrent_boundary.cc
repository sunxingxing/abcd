#include "concurrent_boundary.h"
namespace abcd::source {
std::variant<std::uint64_t,ConcurrentFailure> VerifyConcurrentBoundary(
    const analysis::ValueProgram& graph,const CellPlan* cells,analysis::SemanticBudget budget) {
  const auto fail=[](ConcurrentError kind){ConcurrentFailure e;e.kind=kind;return e;};
  const auto size=cells?cells->cells.size():0;
  if(size>budget.work||graph.ops.size()>budget.work-size)return fail(ConcurrentError::kWorkLimit);
  if(cells)for(const auto& cell:cells->cells)
    if(std::holds_alternative<IncomingCell>(cell.storage))return fail(ConcurrentError::kCaptures);
  for(std::size_t i=0;i<graph.ops.size();++i) {
    const auto& op=graph.ops[i];if(op.life!=analysis::ValueLife::kLive)continue;
    const auto* read=std::get_if<analysis::ValueModuleRead>(&op.operation);
    if((read&&read->space!=analysis::ModuleSpace::kExternal)||std::holds_alternative<analysis::ValueModuleWrite>(op.operation)) {
      auto failure=fail(ConcurrentError::kCaptures);failure.operation=analysis::OpId{static_cast<std::uint32_t>(i)};
      if(op.origin.value<graph.origins.size()){const auto& origin=graph.origins[op.origin.value];failure.pc=origin.original.value_or(origin.pc);failure.working_pc=origin.pc;}
      return failure;
    }
  }
  return size+graph.ops.size();
}
}
