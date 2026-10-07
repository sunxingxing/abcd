#include "definition_elision.h"
#include "tree_checks.h"
#include <stdexcept>
namespace abcd::source {
std::variant<DefinitionElisions,TreeStructureError> PlanDefinitionElisions(
    const RecoveryFlow& flow,const FlowDemand& demand,analysis::SemanticBudget allowance) {
  try {
    TreeCheckBudget budget(allowance);DefinitionElisions result;
    budget.Array(result.writes,flow.definitions.size());std::size_t count=0;
    for(std::size_t i=0;i<flow.definitions.size();++i) {
      budget.Step();const auto& definition=flow.definitions[i];
      if(!demand.definitions[i] && definition.kind==FlowDefinitionKind::kAssignment &&
          definition.effect==FlowWriteEffect::kInert && definition.write)
        result.writes[count++]=*definition.write;
    }
    result.writes.resize(count);count=0;
    budget.Array(result.statements,flow.discarded.size());
    for(std::size_t i=0;i<flow.discarded.size();++i) {
      budget.Step();if(!demand.retained[i])result.statements[count++]=flow.discarded[i].statement;
    }
    result.statements.resize(count);result.allocated_bytes=budget.Bytes();result.work=budget.Work();return result;
  } catch(TreeStructureError error){return error;}
  catch(const std::bad_alloc&){return TreeStructureError::kResourceLimit;}
  catch(const std::length_error&){return TreeStructureError::kResourceLimit;}
}
std::variant<BindingElisions,TreeStructureError> PlanBindingElisions(
    const RecoveryFlow& flow,const DefinitionElisions& delta,analysis::SemanticBudget allowance) {
  try {
    TreeCheckBudget budget(allowance);BindingElisions result;
    std::vector<std::uint8_t> removed_statements,removed_writes,retained;
    budget.Array(removed_statements,flow.statements.size());
    budget.Array(removed_writes,flow.expressions.size());
    budget.Array(retained,flow.bindings.size());
    for(const auto statement:delta.statements){budget.Step();removed_statements[statement.value]=1;}
    for(const auto write:delta.writes){budget.Step();removed_writes[write.value]=1;}
    // Visit semantic occurrences, including unreachable retained syntax: even
    // that syntax needs its lexical binding. No reachability/name/shape census.
    for(const auto& event:flow.events) {
      budget.Step();
      if(event.discarded && removed_statements[flow.discarded[event.discarded->value].statement.value])continue;
      if(event.read)retained[event.read->value]=1;
      if(event.definition) {
        const auto& definition=flow.definitions[event.definition->value];
        if(definition.kind==FlowDefinitionKind::kUndefined)continue;
        if(definition.write && removed_writes[definition.write->value])continue;
        retained[definition.binding.value]=1;
      }
    }
    budget.Array(result.declarations,flow.definitions.size());std::size_t count=0;
    for(const auto& definition:flow.definitions) {
      budget.Step();
      if(definition.kind==FlowDefinitionKind::kUndefined && definition.declaration &&
          flow.bindings[definition.binding.value].exposure==FlowBindingExposure::kPrivate &&
          !retained[definition.binding.value])result.declarations[count++]=*definition.declaration;
    }
    result.declarations.resize(count);result.allocated_bytes=budget.Bytes();result.work=budget.Work();return result;
  } catch(TreeStructureError error){return error;}
  catch(const std::bad_alloc&){return TreeStructureError::kResourceLimit;}
  catch(const std::length_error&){return TreeStructureError::kResourceLimit;}
}
}
