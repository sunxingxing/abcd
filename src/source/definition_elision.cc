#include "definition_elision.h"
#include "tree_checks.h"
#include <stdexcept>
namespace abcd::source {
std::variant<DefinitionElisions,TreeStructureError> PlanDefinitionElisions(
    const RecoveryFlow& flow,const FlowLiveness& live,analysis::SemanticBudget allowance) {
  try {
    TreeCheckBudget budget(allowance);DefinitionElisions result;
    budget.Array(result.writes,flow.definitions.size());std::size_t count=0;
    for (std::size_t i=0;i<flow.definitions.size();++i) {
      budget.Step();const auto& definition=flow.definitions[i];
      if (!live.live_definitions[i] && definition.kind==FlowDefinitionKind::kAssignment &&
          definition.effect==FlowWriteEffect::kInert && definition.write)
        result.writes[count++]=*definition.write;
    }
    result.writes.resize(count);result.allocated_bytes=budget.Bytes();result.work=budget.Work();return result;
  } catch(TreeStructureError error){return error;}
  catch(const std::bad_alloc&){return TreeStructureError::kResourceLimit;}
  catch(const std::length_error&){return TreeStructureError::kResourceLimit;}
}
}
