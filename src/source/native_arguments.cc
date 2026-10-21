#include "native_arguments.h"
#include <limits>

namespace abcd::source {
std::variant<NativeArgumentsProof,NativeArgumentsError> ProveNativeArguments(
    const analysis::ValueProgram& graph,CallableKind callable,TreeCheckBudget& budget) {
  using namespace analysis;
  using Error=NativeArgumentsError;
  if(IsArrowCallable(callable)||!graph.arguments) return Error::kOwner;
  if(graph.ops.size()>UINT32_MAX || graph.values.size()>UINT32_MAX || graph.blocks.size()>UINT32_MAX)
    throw TreeStructureError::kResourceLimit;
  std::optional<OpId> snapshot;
  for(std::uint32_t i=0;i<graph.ops.size();++i) {
    budget.Step();const auto& op=graph.ops[i];
    if(op.life!=ValueLife::kLive||!std::holds_alternative<ValueArgumentSnapshot>(op.operation))continue;
    if(snapshot)return Error::kMultiplicity;snapshot=OpId{i};
  }
  if(!snapshot||graph.blocks.empty())return Error::kEntry;
  std::vector<std::uint8_t> seen;budget.Array(seen,graph.blocks.size());
  std::vector<std::optional<WorkBlockId>> predecessor;budget.Array(predecessor,graph.blocks.size());
  WorkBlockId block{0};bool found=false;
  while(!found) {
    budget.Step();if(block.value>=graph.blocks.size()||seen[block.value])return Error::kEntry;
    seen[block.value]=1;const auto& body=graph.blocks[block.value];
    if(body.first_op.value>graph.ops.size() || body.op_count>graph.ops.size()-body.first_op.value ||
        body.first_edge.value>graph.edges.size() || body.edge_count>graph.edges.size()-body.first_edge.value)
      return Error::kCorrespondence;
    for(std::uint32_t i=0;i<body.op_count;++i) {
      budget.Step();const auto at=OpId{body.first_op.value+i};if(at.value>=graph.ops.size())return Error::kCorrespondence;
      const auto& op=graph.ops[at.value];if(op.life!=ValueLife::kLive)continue;
      if(at==*snapshot){found=true;break;}
      // Environment allocation and frame transport are private invocation
      // setup, not an observable access. Allocation failure is excluded by V6.
      const bool environment=std::holds_alternative<ValueEnvironmentPush>(op.operation)||
          std::holds_alternative<ValueCurrentEnvironment>(op.operation);
      if(environment)continue;
      if(op.normal!=LanguageEffect::kNone||op.before_throw!=LanguageEffect::kNone||
          op.exceptional!=ExceptionalCompletion::kImpossible||
          std::holds_alternative<ValueBranch>(op.operation)||
          std::holds_alternative<ValueReturn>(op.operation)||
          std::holds_alternative<ValueThrow>(op.operation))return Error::kEntry;
    }
    if(found)break;
    std::optional<WorkBlockId> next;
    for(std::uint32_t i=0;i<body.edge_count;++i){budget.Step();const auto& edge=graph.edges[body.first_edge.value+i];if(edge.kind==FlowEdgeKind::kException)return Error::kEntry;if(next)return Error::kEntry;next=edge.target;}
    if(!next || next->value>=predecessor.size())return Error::kEntry;predecessor[next->value]=block;block=*next;
  }
  for(const auto& edge:graph.edges) {
    budget.Step();if(edge.target.value>=seen.size())return Error::kCorrespondence;
    if(seen[edge.target.value] && predecessor[edge.target.value]!=edge.source)return Error::kEntry;
  }
  const auto& op=graph.ops[snapshot->value];
  if(op.use_count!=graph.arguments->frame.parameters || std::get<ValueArgumentSnapshot>(op.operation).slots!=op.use_count)
    return Error::kParameter;
  if(op.first_use.value>graph.uses.size() || op.use_count>graph.uses.size()-op.first_use.value)
    return Error::kCorrespondence;
  // DefaultBody deliberately clears original value correspondence: its entry
  // formals are initialized source parameters, not incoming argument slots.
  // Do not mistake a projected ParameterEntry for an immutable supplied value.
  if(op.use_count && graph.input.values.empty())return Error::kParameter;
  constexpr auto unknown=std::numeric_limits<std::uint32_t>::max();
  constexpr auto invalid=unknown-1;
  std::vector<std::uint32_t> roots;budget.Array(roots,graph.values.size());
  budget.Step(roots.size());std::fill(roots.begin(),roots.end(),unknown);
  std::vector<std::uint32_t> path;budget.Admit(graph.values.size(),sizeof(std::uint32_t));path.reserve(graph.values.size());
  budget.Admit(path.capacity()-graph.values.size(),sizeof(std::uint32_t));
  for(std::uint32_t i=0;i<op.use_count;++i) {
    budget.Step();const auto* value=std::get_if<ValueId>(&graph.uses[op.first_use.value+i].operand);
    if(!value||value->value>=roots.size())return Error::kParameter;
    auto at=value->value;path.clear();std::uint32_t root=invalid;
    while(roots[at]==unknown) {
      budget.Step();roots[at]=invalid;path.push_back(at);
      const auto& definition=graph.values[at];
      if(const auto* entry=std::get_if<EntryDefinition>(&definition.origin)) {
        if(const auto* parameter=std::get_if<ParameterEntry>(entry))root=parameter->index;
        break;
      }
      const auto* result=std::get_if<OpResult>(&definition.origin);
      if(!result||result->op.value>=graph.ops.size())break;
      const auto& copy=graph.ops[result->op.value];
      if(!std::holds_alternative<ValueCopy>(copy.operation)||copy.use_count!=1||copy.first_use.value>=graph.uses.size())break;
      const auto* input=std::get_if<ValueId>(&graph.uses[copy.first_use.value].operand);
      if(!input||input->value>=roots.size())break;at=input->value;
      if(roots[at]!=unknown)root=roots[at];
    }
    if(path.empty())root=roots[at];
    for(auto id:path){budget.Step();roots[id]=root;}
    if(root!=i)return Error::kParameter;
  }
  return NativeArgumentsProof{*snapshot,graph.revision,*graph.arguments};
}
bool VerifyNativeArguments(const analysis::ValueProgram& graph,CallableKind callable,
    const NativeArgumentsProof& proof,TreeCheckBudget& budget) {
  const auto fresh=ProveNativeArguments(graph,callable,budget);
  const auto* expected=std::get_if<NativeArgumentsProof>(&fresh);
  return expected&&*expected==proof;
}
void VerifyNativeArgumentsNames(const FunctionTree& tree,TreeCheckBudget& budget) {
  // Native arguments is owned by THIS function, never a free/global name.
  // Captured snapshots in another non-arrow function retain a named source
  // storage; they are not rewritten to that descendant's invocation carrier.
  for(const auto& symbol:tree.symbols){budget.Step();RequireTree(symbol.name.value<tree.names.size(),TreeStructureError::kName);const auto& name=tree.names[symbol.name.value];budget.Step(name.size());RequireTree(name!=u"arguments",TreeStructureError::kBinding);}
}
}  // namespace abcd::source
