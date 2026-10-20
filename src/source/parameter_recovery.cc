#include "parameter_recovery.h"
#include "tree_flow.h"
#include <new>
namespace abcd::source {
namespace {
constexpr auto kInvalid = TreeStructureError::kOwnership;
void Require(bool ok) { RequireTree(ok,kInvalid); }
template<class E> void Failure(E error, E resource, E work) {
  throw error==resource?TreeStructureError::kResourceLimit:error==work?TreeStructureError::kWorkLimit:kInvalid;
}
}
void VerifyParameterDefaultInput(const FunctionTree& tree,
    const OriginalParameterDefaults& source, TreeCheckBudget& budget) {
  Require(source.input && source.formal_count==tree.parameters.size());
  const auto& g=*source.input;
  auto env=analysis::BuildValueEnvironments(g,budget.Remaining());
  const auto* environments=std::get_if<analysis::ValueEnvironments>(&env);
  if(!environments)Failure(std::get<analysis::ValueEnvironmentError>(env),analysis::ValueEnvironmentError::kResourceLimit,analysis::ValueEnvironmentError::kWorkLimit);
  budget.Admit(environments->allocated_bytes,1);budget.Step(environments->work);
  auto cells=BuildCellPlan(g,*environments,budget.Remaining());const auto* cell=std::get_if<CellPlan>(&cells);
  if(!cell)Failure(std::get<CellError>(cells),CellError::kResourceLimit,CellError::kWorkLimit);
  budget.Admit(cell->allocated_bytes,1);budget.Step(cell->work);
  auto parameters=BuildParameterPlan(g,source.formal_count,budget.Remaining());const auto* parameter=std::get_if<ParameterPlan>(&parameters);
  if(!parameter)Failure(std::get<ParameterPlanError>(parameters),ParameterPlanError::kResourceLimit,ParameterPlanError::kWorkLimit);
  budget.Admit(parameter->allocated_bytes,1);budget.Step(parameter->work);
  std::vector<CellParameterDefault> proofs;budget.Array(proofs,source.entries.size());
  std::optional<std::uint32_t> previous;
  for(std::size_t i=0;i<proofs.size();++i) {
    budget.Step();const auto& item=source.entries[i];proofs[i]=item.proof;
    const auto ordinal=item.proof.parameter;
    Require(ordinal<tree.parameters.size()&&(!previous||*previous<ordinal));previous=ordinal;
    const auto& p=tree.parameters[ordinal];
    Require(!p.rest&&p.symbol==item.parameter&&p.initializer==item.initializer);
    Require(item.initializer.value<tree.expressions.size()&&item.outer!=item.parameter);
    const auto* read=std::get_if<SymbolExpr>(&tree.expressions[item.initializer.value].expression);
    Require(read&&read->symbol==item.outer&&item.outer.value<tree.symbols.size()&&tree.symbols[item.outer.value].kind==BindingKind::kCapture);
  }
  auto checked=VerifyCellParameterDefaults(g,*environments,*cell,*parameter,source.length,proofs,budget.Remaining());
  if(const auto* error=std::get_if<CellParameterDefaultFailure>(&checked))Failure(error->kind,CellParameterDefaultError::kResourceLimit,CellParameterDefaultError::kWorkLimit);
  const auto cost=std::get<CellParameterDefaultCost>(checked);budget.Admit(cost.allocated_bytes,1);budget.Step(cost.work);
}
std::variant<DefaultSignatureProof,DefaultSignatureError> RetainParameterDefaults(
    const DefaultBody& body,const FunctionTree& tree,TreeFlow& flow,
    const DefaultSignatureContext& context,analysis::SemanticBudget limit) {
  try {
    TreeCheckBudget budget(limit);
    auto signature=VerifyDefaultSignature(body,tree,budget.Remaining(),&context);
    if(const auto* error=std::get_if<DefaultSignatureError>(&signature))return *error;
    const auto cost=std::get<DefaultSignatureProof>(signature);budget.Admit(cost.allocated_bytes,1);budget.Step(cost.work);
    Require(body.CellParameters()&&flow.original&&flow.original.use_count()==1);
    OriginalParameterDefaults source;source.input=body.CellParameterOriginal();source.formal_count=body.FormalCount();source.length=body.Length();
    // The retained immutable input outlives DefaultBody; account its ownership
    // conservatively rather than borrowing a soon-to-be-destroyed arena.
    budget.Admit(body.AllocatedBytes(),1);
    budget.Array(source.entries,body.CellParameters()->Defaults().size());
    for(std::size_t i=0;i<source.entries.size();++i) {
      budget.Step();const auto& d=body.CellParameters()->Defaults()[i];const auto& p=tree.parameters[d.parameter];
      Require(p.initializer.has_value());const auto* read=std::get_if<SymbolExpr>(&tree.expressions[p.initializer->value].expression);Require(read);
      source.entries[i]={d,*p.initializer,p.symbol,read->symbol};
    }
    VerifyParameterDefaultInput(tree,source,budget);
    // This owner was freshly made mutable by VerifyTreeSemantics and has not
    // escaped. Freeze it again before handing it to recovery projections.
    std::const_pointer_cast<OriginalFlow>(flow.original)->parameter_defaults=std::move(source);
    return DefaultSignatureProof{tree.value_revision,tree.cfg_revision,budget.Bytes(),budget.Work()};
  }catch(TreeStructureError error){return error==TreeStructureError::kResourceLimit?DefaultSignatureError::kResourceLimit:error==TreeStructureError::kWorkLimit?DefaultSignatureError::kWorkLimit:DefaultSignatureError::kCellParameterRecovery;}
  catch(const std::bad_alloc&){return DefaultSignatureError::kResourceLimit;}
}
void VerifyParameterDefaultFlow(const FunctionTree& tree,const RecoveryFlow& flow,TreeCheckBudget& budget) {
  if (tree.parameter_expressions) {
    VerifyParameterExpressionFlow(tree, flow, budget);
    return;
  }
  if(!flow.original||!flow.original->parameter_defaults){Require(flow.parameter_defaults.empty());return;}
  const auto& source=*flow.original->parameter_defaults;
  Require(flow.parameter_defaults.size()==source.entries.size());
  Require(flow.original->cells.has_value()&&source.input);
  Require(flow.original->value_revision.value==source.input->revision.value+1&&flow.original->cfg_revision.value==source.input->cfg_revision.value+1);
  std::vector<const OriginalCellBinding*> binding_cells;
  budget.Array(binding_cells,tree.symbols.size());
  for(const auto& binding:flow.original->cells->bindings) {
    budget.Step();Require(binding.binding.value<binding_cells.size());
    // A source alias never proves distinct original cell identities.
    Require(!binding_cells[binding.binding.value]);binding_cells[binding.binding.value]=&binding;
  }
  std::vector<std::optional<FlowPoint>> commits,starts;
  budget.Array(commits,tree.parameters.size());budget.Array(starts,tree.parameters.size());
  std::vector<std::optional<std::uint32_t>> ordinals;budget.Array(ordinals,tree.symbols.size());
  for(std::uint32_t i=0;i<tree.parameters.size();++i){budget.Step();Require(tree.parameters[i].symbol.value<ordinals.size());Require(!ordinals[tree.parameters[i].symbol.value]);ordinals[tree.parameters[i].symbol.value]=i;}
  for(const auto& definition:flow.definitions) {
    budget.Step();if(definition.kind!=FlowDefinitionKind::kParameter)continue;
    Require(definition.binding.value<ordinals.size()&&ordinals[definition.binding.value]);const auto i=*ordinals[definition.binding.value];
    Require(!commits[i]);commits[i]=starts[i]=definition.point;
  }
  for(const auto& port:flow.parameter_defaults){budget.Step();Require(port.parameter.value<starts.size());starts[port.parameter.value]=port.argument;}
  // Exact outgoing sets prove branch exclusivity, no extra commits, and that
  // the missing-path read cannot enter a body handler. Use graph adjacency.
  const auto edges=[&](FlowPoint from,std::initializer_list<std::pair<FlowPoint,FlowCompletion>> expected) {
    Require(from.value<flow.events.size());std::size_t count=0;unsigned seen=0;
    for(auto at=flow.events[from.value].outgoing;at;at=flow.edges[*at].outgoing) {
      budget.Step();Require(*at<flow.edges.size()&&count<expected.size());const auto& edge=flow.edges[*at];Require(edge.from==from);bool found=false;unsigned ordinal=0;
      for(const auto& [to,kind]:expected){if(edge.to==to&&edge.completion==kind){Require(!(seen&(1U<<ordinal)));seen|=1U<<ordinal;found=true;}++ordinal;}
      Require(found);++count;
    }
    Require(count==expected.size());
  };
  for(std::size_t i=0;i<source.entries.size();++i) {
    budget.Step();const auto& input=source.entries[i];const auto& port=flow.parameter_defaults[i];
    Require(input.outer.value<binding_cells.size()&&binding_cells[input.outer.value]);
    const auto& storage=binding_cells[input.outer.value]->storage;
    const auto* depth=std::get_if<analysis::CapturedDepth>(&storage.environment);
    Require(depth&&*depth==input.proof.source.depth&&storage.slot.value==input.proof.source.slot.value);
    const FlowPoint points[]{port.argument,port.decision,port.provided,port.missing,port.initialized,port.caller_exception};
    for(std::size_t a=0;a<6;++a)for(std::size_t b=0;b<a;++b)Require(points[a]!=points[b]);
    Require(port.parameter.value==input.proof.parameter&&port.initializer==input.initializer&&port.caller_exception==flow.terminal);
    Require(port.initialized.value<flow.events.size()&&flow.events[port.initialized.value].definition.has_value());
    const auto definition=*flow.events[port.initialized.value].definition;
    Require(definition.value<flow.definitions.size()&&flow.definitions[definition.value].binding==input.parameter&&flow.definitions[definition.value].kind==FlowDefinitionKind::kParameter);
    Require(commits[port.parameter.value]==port.initialized);
    for(auto point:{port.argument,port.decision,port.provided,port.missing}) {
      Require(point.value<flow.events.size());const auto& e=flow.events[point.value];
      Require(e.kind==FlowEventKind::kControl&&!e.read&&!e.definition&&!e.expression&&!e.effects);
    }
    const auto ordinal=port.parameter.value;
    if(ordinal){Require(commits[ordinal-1].has_value());edges(*commits[ordinal-1],{{port.argument,FlowCompletion::kNormal}});}
    else edges(flow.entry,{{port.argument,FlowCompletion::kNormal}});
    if(ordinal+1<starts.size()){Require(starts[ordinal+1].has_value());edges(port.initialized,{{*starts[ordinal+1],FlowCompletion::kNormal}});}
    else edges(port.initialized,{{flow.statements[tree.body.value],FlowCompletion::kNormal}});
    edges(port.argument,{{port.decision,FlowCompletion::kNormal}});
    edges(port.decision,{{port.provided,FlowCompletion::kNotTaken},{flow.expressions[input.initializer.value],FlowCompletion::kTaken}});
    edges(port.provided,{{port.initialized,FlowCompletion::kNormal}});
    edges(port.missing,{{port.initialized,FlowCompletion::kNormal}});
    Require(input.initializer.value<flow.actions.size()&&flow.actions[input.initializer.value]);
    const auto& action=*flow.actions[input.initializer.value];
    Require(action.before.value<flow.events.size());const auto& read_event=flow.events[action.before.value];
    Require(read_event.kind==FlowEventKind::kRead&&read_event.read==input.outer&&read_event.expression==input.initializer&&read_event.entry_evaluation);
    Require(read_event.effects&&read_event.effects->exceptional==analysis::ExceptionalCompletion::kMayThrow);
    Require(action.normal==port.missing&&action.exceptions.thrown==flow.terminal&&!action.exceptions.finalizer);
    const auto start=flow.expressions[input.initializer.value];
    if(start!=action.before)edges(start,{{action.before,FlowCompletion::kNormal}});
    edges(action.before,{{port.missing,FlowCompletion::kNormal},{flow.terminal,FlowCompletion::kThrow}});
  }
}
}
