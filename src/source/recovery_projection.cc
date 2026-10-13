#include "binding_normalization.h"
#include "recovery_projection.h"
#include "expression_children.h"
#include "flow_kinds.h"
#include <algorithm>
#include <new>
#include <limits>
#include <stdexcept>
namespace abcd::source {
namespace {
const TreeLiteral& Literal(const RecoveryExpressionInput& input,PrimitiveId id) {
  RequireTree(id.value<input.bindings.primitives.size(),TreeStructureError::kOwnership);
  return input.bindings.primitives[id.value];
}
FlowValueEquation Equation(const RecoveryExpressionInput& input,ExprId id) {
  if(const auto transport=input.bindings.transports[id.value])return FlowPrimitiveReference{transport->replacement};
  if(const auto replacement=input.bindings.replacements[id.value])return FlowExpressionValue{*replacement};
  if(const auto primitive=input.bindings.read_literals[id.value])return FlowPrimitiveReference{*primitive};
  if(input.bindings.read_sources[id.value])return FlowBindingValue{*input.bindings.read_sources[id.value]};
  auto equation=input.equations[id.value];
  if(const auto* read=std::get_if<FlowBindingValue>(&equation)) {
    if(const auto primitive=input.bindings.literals[read->binding.value])return FlowPrimitiveReference{*primitive};
    return FlowBindingValue{input.bindings.ReadBinding(read->binding)};
  }
  return equation;
}
void Apply(LegacyFlowProjection& projection,const RecoveryExpressionInput& input,const RecoveryExpressions& expressions) {
  const auto& plan=input.bindings;
  projection.initializers=plan.statements;projection.omitted=plan.omitted;
  projection.statement_values=plan.statement_values;
  projection.undefined_stores=plan.undefined_stores;projection.declaration_values=plan.declaration_values;
  projection.declaration_completions=plan.declaration_completions;
  projection.forwarded_values=plan.forwarded_values;projection.loop_declarations=plan.loop_declarations;
  projection.aliases=plan.aliases;projection.literals=plan.literals;
  projection.storage=plan.storage;projection.read_literals=plan.read_literals;
  projection.read_bindings=plan.read_bindings;projection.read_sources=plan.read_sources;
  projection.replacements=input.bindings.replacements;projection.transports=input.bindings.transports;
  projection.evaluations=expressions.evaluations;projection.normal_stores=expressions.normal_stores;
  projection.normal_bindings=expressions.bindings;projection.normal_kinds=expressions.kinds;
  projection.original=input.original;
}
std::size_t ExpressionBytes(const RecoveryExpressions& expressions) {
  // Capacities are allocations already admitted by Array. Sum only retained
  // expression facts, not the dependency traversal scratch released on return.
  return expressions.bindings.capacity()*sizeof(std::optional<SymbolId>)+
      expressions.literals.capacity()*sizeof(std::optional<PrimitiveId>)+
      expressions.roots.capacity()*sizeof(ExprId)+expressions.normal_stores.capacity()*sizeof(std::uint8_t)+
      expressions.evaluations.capacity()*sizeof(ExpressionEvaluation)+expressions.lexical.capacity()*sizeof(LexicalDependency)+
      expressions.kinds.capacity()*sizeof(analysis::ValueKinds);
}
std::vector<ExprId> DependencyOrder(const FunctionTree& tree,const RecoveryExpressionInput& input,TreeCheckBudget& budget) {
  enum class State : std::uint8_t {kUnseen,kPending,kDone};
  struct Visit {ExprId expression;bool finish=false;};
  std::vector<State> states;budget.Array(states,tree.expressions.size());
  std::size_t children=0;
  for(auto root:input.postorder) {
    budget.Step();RequireTree(root.value<states.size(),TreeStructureError::kOwnership);
    VisitExpressionChildren(tree,tree.expressions[root.value].expression,[&](ExprId){budget.Step();++children;});
  }
  // Each pending expression needs one finish frame; unvisited dependencies
  // need at most one frame per source edge or replacement. Count dimensions,
  // never rescan a subtree per candidate. Invalid dependency cycles have no
  // finite source expression; they are not an admission rule about layout.
  RequireTree(tree.expressions.size()<=std::numeric_limits<std::size_t>::max()/2&&
      children<=std::numeric_limits<std::size_t>::max()-tree.expressions.size()*2,TreeStructureError::kResourceLimit);
  std::vector<Visit> stack;budget.Array(stack,tree.expressions.size()*2+children);
  std::vector<ExprId> order;budget.Array(order,tree.expressions.size());
  std::size_t count=0;
  for(auto root:input.postorder) {
    std::size_t pending=1;stack[0]={root};
    while(pending) {
      budget.Step();const auto visit=stack[--pending];auto& state=states[visit.expression.value];
      if(visit.finish){state=State::kDone;order[count++]=visit.expression;continue;}
      if(state==State::kDone)continue;
      RequireTree(state!=State::kPending,TreeStructureError::kOwnership);state=State::kPending;
      const auto push=[&](Visit next){RequireTree(pending<stack.size(),TreeStructureError::kResourceLimit);stack[pending++]=next;};
      push({visit.expression,true});
      if(input.bindings.transports[visit.expression.value])continue;
      if(const auto replacement=input.bindings.replacements[visit.expression.value]) {
        RequireTree(replacement->value<states.size(),TreeStructureError::kOwnership);push({*replacement});
      } else VisitExpressionChildren(tree,tree.expressions[visit.expression.value].expression,[&](ExprId child){budget.Step();push({child});});
    }
  }
  order.resize(count);return order;
}
}
FlowValueEquation ProjectedRecoveryEquation(const RecoveryExpressionInput& input,ExprId id) {
  return Equation(input,id);
}
bool WritableRecoveryBinding(const FunctionTree& tree,SymbolId symbol) {
  const auto& binding=tree.symbols[symbol.value];
  if(binding.declaration==DeclarationKind::kConst)return false;
  switch(binding.kind) {
    case BindingKind::kLocal:case BindingKind::kCompilerTemporary:
    case BindingKind::kParameter:case BindingKind::kCatchParameter:return true;
    case BindingKind::kCapture:case BindingKind::kModule:case BindingKind::kFunctionSelf:return false;
  }
  throw TreeStructureError::kBinding;
}
RecoveryExpressions AnalyzeRecoveryExpressions(const FunctionTree& tree,
    const RecoveryExpressionInput& input,std::span<const std::uint8_t> initialized,TreeCheckBudget& budget) {
  const auto size=tree.expressions.size();
  RequireTree(input.equations.size()==size&&input.bindings.replacements.size()==size&&input.bindings.transports.size()==size&&
      (initialized.empty()||initialized.size()==size),TreeStructureError::kOwnership);
  RequireTree(input.normal_kinds.empty()||input.normal_kinds.size()==size,TreeStructureError::kOwnership);
  if(input.original)CheckOriginalFlow(tree,*input.original);
  const auto order=DependencyOrder(tree,input,budget);
  RecoveryExpressions facts;
  budget.Array(facts.bindings, tree.expressions.size());
  budget.Array(facts.literals, tree.expressions.size());
  budget.Array(facts.roots, tree.expressions.size());
  budget.Array(facts.normal_stores, tree.expressions.size());
  budget.Array(facts.evaluations, tree.expressions.size());
  budget.Array(facts.lexical,tree.expressions.size());
  budget.Array(facts.kinds,tree.expressions.size());
  auto& stored = facts.bindings;
  // All facts describe one unchanged projection. The analysis is const and
  // publishes no plan state; proposal/commit consumers own all mutations.
  // Replacement dependencies join canonical child dependencies. No original
  // arena or statement ordering is required for projected value facts.
  for (const auto id:order) {
    budget.Step();
    const auto& node = tree.expressions[id.value].expression;
    // Normal-completion fact: the result is still the value of this binding.
    // ECMA-262 assignment/prefix update write before returning that value;
    // comma forwards its right completion, and choice joins must agree.
    // Unknown calls/coercions/suspensions produce no fact. No effect moves,
    // and a throwing/TDZ RHS still executes before any projected outer store.
    // Consume projected dependency order, never arena order or a local syntax match.
    auto& result = stored[id.value];facts.roots[id.value]=id;
    auto& literal=facts.literals[id.value];
    auto& kinds=facts.kinds[id.value];
    kinds=!input.normal_kinds.empty()?input.normal_kinds[id.value]:
        NormalValueKinds(node,facts.kinds);
    const auto join = [&](ExprId left, ExprId right) -> std::optional<SymbolId> {
      return stored[left.value] == stored[right.value] ? stored[left.value] : std::nullopt;
    };
    if(const auto transport=input.bindings.transports[id.value]) {
      literal=transport->replacement;kinds=PrimitiveKinds(Literal(input,*literal));
      facts.evaluations[id.value]=ExpressionEvaluation::kInert;
      facts.lexical[id.value]=LexicalDependency::kReferences;
      continue;
    }
    if (const auto replacement = input.bindings.replacements[id.value]) {
      facts.roots[id.value]=facts.roots[replacement->value];
      result = stored[replacement->value];
      literal=facts.literals[replacement->value];
      kinds=facts.kinds[replacement->value];
      facts.evaluations[id.value] = facts.evaluations[replacement->value];
      facts.lexical[id.value]=facts.lexical[replacement->value];
      continue;
    }
    const auto equation=ProjectedRecoveryEquation(input,id);
    auto evaluation = NormalValueEvaluation(node,facts.kinds,ResourceFailures(tree.identity.contract));
    auto lexical=IntrinsicLexicalDependency(node);
    if (const auto* read = std::get_if<SymbolExpr>(&node)) {
      literal=input.bindings.ReadLiteral(id,read->symbol);
      if (!literal) {
        const auto source = input.bindings.ReadBinding(id,read->symbol);
        result = source;
      }
      if (read->loading == analysis::ModuleLoading::kOrdinary && (literal || (!initialized.empty() && initialized[id.value])))
        evaluation = ExpressionEvaluation::kInert;
    } else if (const auto* assignment = std::get_if<AssignmentExpr>(&node)) {
      literal=facts.literals[assignment->value.value];
      if (const auto* target = std::get_if<SymbolTarget>(&assignment->target);
          target && WritableRecoveryBinding(tree,target->symbol)) {
        result = input.bindings.Storage(target->symbol);
        facts.normal_stores[id.value] = !initialized.empty() && initialized[id.value];
      }
    } else if (const auto* update = std::get_if<UpdateExpr>(&node)) {
      if (update->result == UpdateResult::kPrefix && WritableRecoveryBinding(tree,update->symbol)) result = input.bindings.Storage(update->symbol);
    } else if (const auto* sequence = std::get_if<SequenceExpr>(&node)) result = stored[sequence->right.value];
    else if (const auto* choice = std::get_if<ConditionalExpr>(&node)) result = join(choice->consequent, choice->alternate);
    else if (const auto* logical = std::get_if<LogicalExpr>(&node)) result = join(logical->left, logical->right);
    else if (const auto* receiver = std::get_if<ThisExpr>(&node)) {
      if (receiver->owner == tree.owner && tree.callable != CallableKind::kDerivedConstructor &&
          !IsArrowCallable(tree.callable)) evaluation = ExpressionEvaluation::kInert;
    }
    // ECMA-262 completion values: sequence/assignment forward a value;
    // conditional joins equal values or selects a known primitive condition;
    // logical selection uses ToBoolean/nullish, never user coercion. Unknown
    // operators/calls/updates/suspensions retain no primitive fact. Effects,
    // initialization and source ownership are still analyzed separately.
    std::visit([&](const auto& value) {
      using T=std::decay_t<decltype(value)>;
      if constexpr(std::is_same_v<T,FlowPrimitiveReference>)literal=value.primitive;
      else if constexpr(std::is_same_v<T,FlowExpressionValue>)literal=facts.literals[value.expression.value];
      else if constexpr(std::is_same_v<T,FlowChoiceValue>) {
        const auto a=facts.literals[value.consequent.value],b=facts.literals[value.alternate.value];
        if(const auto condition=facts.literals[value.condition.value]) {
          const auto selected=PrimitiveTruth(Literal(input,*condition),tree.texts)?value.consequent:value.alternate;
          literal=facts.literals[selected.value];result=stored[selected.value];
        } else if(a&&b&&SamePrimitiveValue(Literal(input,*a),Literal(input,*b)))literal=a;
      } else if constexpr(std::is_same_v<T,FlowLogicalValue>) {
        if(const auto left=facts.literals[value.left.value]) {
          bool right=false;
          switch(value.operation) {
            case TreeLogical::kAnd:right=PrimitiveTruth(Literal(input,*left),tree.texts);break;
            case TreeLogical::kOr:right=!PrimitiveTruth(Literal(input,*left),tree.texts);break;
            case TreeLogical::kNullish:right=PrimitiveNullish(Literal(input,*left));break;
          }
          literal=right?facts.literals[value.right.value]:left;
          result=stored[(right?value.right:value.left).value];
        }
      } else static_assert(std::is_same_v<T,UnknownFlowValue>||std::is_same_v<T,FlowBindingValue>);
    },equation);
    if(literal)kinds=PrimitiveKinds(Literal(input,*literal));
    else {
      if(const auto* value=std::get_if<FlowExpressionValue>(&equation))kinds=facts.kinds[value->expression.value];
    }
    // Normal domains belong to this projection's success definitions. Raw
    // operand domains cannot survive a changed producer merely because the
    // read occurrence is unchanged. GetValue/effect completion stay separate.
    VisitExpressionChildren(tree, node, [&](ExprId child) {
      budget.Step(); if (facts.evaluations[child.value] == ExpressionEvaluation::kRequired)
        evaluation = ExpressionEvaluation::kRequired;
      switch(facts.lexical[child.value]) {
        case LexicalDependency::kReferences:break;
        case LexicalDependency::kCallableOwner:
          if(lexical==LexicalDependency::kReferences)lexical=LexicalDependency::kCallableOwner;
          break;
        case LexicalDependency::kEnclosingEnvironment:lexical=LexicalDependency::kEnclosingEnvironment;break;
      }
    });
    facts.evaluations[id.value] = evaluation;facts.lexical[id.value]=lexical;
  }
  return facts;
}
std::variant<RecoveryProjection,TreeStructureError> PrepareRecoveryProjection(
    const FunctionTree& tree,LegacyFlowProjection projection,const RecoveryExpressionInput& supplied,
    analysis::SemanticBudget allowance) {
  try {
    // Only this bootstrap may attach a completed domain product. Accepting an
    // external span here would let a trial borrow another plan's type proof.
    RequireTree(supplied.normal_kinds.empty(),TreeStructureError::kOwnership);
    TreeCheckBudget budget(allowance);RecoveryProjection result;auto input=supplied;
    const auto normalized=NormalizeBindingProjection(tree.symbols.size(),input.bindings.aliases,input.bindings.storage,budget);
    if(normalized){input.bindings.aliases=normalized->aliases;input.bindings.storage=normalized->storage;}
    const auto normalization_bytes=budget.ActiveBytes();
    std::size_t peak=budget.Bytes();
    const auto remaining=[&](std::size_t live) {
      RequireTree(normalization_bytes<=allowance.bytes&&live<=allowance.bytes-normalization_bytes,TreeStructureError::kResourceLimit);
      return analysis::SemanticBudget{allowance.bytes-normalization_bytes-live,budget.Remaining().work};
    };
    const auto charge=[&](std::size_t live,std::size_t bytes,std::uint64_t work) {
      RequireTree(bytes<=remaining(live).bytes,TreeStructureError::kResourceLimit);
      peak=std::max(peak,normalization_bytes+live+bytes);budget.Step(work);
    };
    // Sequential scratch is released at these ownership boundaries. Every
    // phase is admitted against ALL simultaneously live output/scratch. Work
    // remains cumulative. Reporting cumulative freed bytes as retained storage
    // would exhaust the later printer without representing real peak memory.
    {
      TreeCheckBudget expression_budget(remaining(0));
      const auto conservative=AnalyzeRecoveryExpressions(tree,input,{},expression_budget);
      charge(0,expression_budget.Bytes(),expression_budget.Work());
      Apply(projection,input,conservative);
      const auto live=ExpressionBytes(conservative);
      auto built=BuildRecoveryFlow(tree,projection,remaining(live));
      if(const auto* error=std::get_if<TreeStructureError>(&built))throw *error;
      result.flow=std::get<RecoveryFlow>(std::move(built));
      charge(live,result.flow.allocated_bytes,result.flow.work);
    }
    // Provider accounting is a conservative bound on its retained flow; using
    // it as the live charge includes all owned vectors and control metadata.
    const auto flow_bytes=result.flow.retained_bytes;
    FlowInitializationProgram initialization_program;
    {
      auto prepared=BuildFlowInitializationProgram(result.flow,remaining(flow_bytes));
      if(const auto* error=std::get_if<TreeStructureError>(&prepared))throw *error;
      initialization_program=std::get<FlowInitializationProgram>(std::move(prepared));
      charge(flow_bytes,initialization_program.allocated_bytes,initialization_program.work);
      TreeCheckBudget reads_budget(remaining(flow_bytes+initialization_program.retained_bytes));
      reads_budget.Array(result.initialized_reads,tree.expressions.size());
      charge(flow_bytes+initialization_program.retained_bytes,reads_budget.Bytes(),reads_budget.Work());
    }
    const auto retained=flow_bytes+result.initialized_reads.capacity()*sizeof(std::uint8_t)+initialization_program.retained_bytes;
    FlowNormalKinds domains;
    // Every projection solves initialization and source values against the
    // same executable completions. Original evidence refines proven raw
    // identities when present; its absence does not select another solver.
    {
      TreeCheckBudget read_budget(remaining(retained));
      const auto reads=AnalyzeRecoveryExpressions(tree,input,result.initialized_reads,read_budget);
      charge(retained,read_budget.Bytes(),read_budget.Work());
      const auto live=retained+ExpressionBytes(reads);
      // Joint clauses keep the conservative graph's edge identity until the
      // solve finishes. Early compaction would invalidate their guards.
      {
        auto analyzed=BuildFlowValueStates(result.flow,remaining(live));
        if(const auto* error=std::get_if<TreeStructureError>(&analyzed))throw *error;
        const auto value_states=std::get<FlowValueStates>(std::move(analyzed));
        charge(live,value_states.allocated_bytes,value_states.work);
        const auto analysis_live=live+value_states.retained_bytes;
        TreeCheckBudget assignment_budget(remaining(analysis_live));
        std::vector<ExpressionEvaluation> assignments;
        assignment_budget.Array(assignments,tree.expressions.size());
        for(auto& effect:assignments){assignment_budget.Step();effect=ExpressionEvaluation::kRequired;}
        for(const auto& definition:result.flow.definitions) {
          assignment_budget.Step();if(definition.write&&(definition.kind==FlowDefinitionKind::kAssignment||
              definition.kind==FlowDefinitionKind::kInitializer))
            assignments[definition.write->value]=definition.effect==FlowWriteEffect::kInert?
                ExpressionEvaluation::kInert:ExpressionEvaluation::kRequired;
        }
        charge(analysis_live,assignment_budget.Bytes(),assignment_budget.Work());
        const auto typed_live=analysis_live+assignment_budget.Bytes();
        const FlowEffectInput completion{reads.evaluations,reads.normal_stores,assignments,{}};
        auto typed=AnalyzeFlowNormalKinds(tree,result.flow,value_states,input,remaining(typed_live),&completion,&initialization_program);
        if(const auto* error=std::get_if<TreeStructureError>(&typed))throw *error;
        domains=std::get<FlowNormalKinds>(std::move(typed));
        charge(typed_live,domains.allocated_bytes,domains.work);
      }
    }
    {
      budget.Step(result.flow.events.size()+result.initialized_reads.size()+result.flow.captured_inputs.size());
      RequireTree(domains.initialized_reads.size()==result.initialized_reads.size()&&domains.read_completions.size()==result.flow.events.size(),TreeStructureError::kOwnership);
      std::copy(domains.initialized_reads.begin(),domains.initialized_reads.end(),result.initialized_reads.begin());
      RequireTree(domains.original_effects.size()==result.flow.events.size(),TreeStructureError::kOwnership);
      for(std::size_t i=0;i<result.flow.events.size();++i) {
        result.flow.events[i].read_completion=domains.read_completions[i];
        result.flow.events[i].original_effect_valid=domains.original_effects[i];
      }
      RequireTree(domains.captured_inputs.size()==result.flow.captured_inputs.size(),TreeStructureError::kOwnership);
      for(std::size_t i=0;i<domains.captured_inputs.size();++i)result.flow.captured_inputs[i].kinds=domains.captured_inputs[i];
    }
    const auto domains_bytes=domains.expressions.capacity()*sizeof(analysis::ValueKinds)+domains.executable.capacity()*sizeof(std::uint8_t)+
        domains.truths.capacity()*sizeof(TruthDomains)+
        domains.initialized_reads.capacity()*sizeof(std::uint8_t)+domains.read_completions.capacity()*sizeof(FlowReadCompletion)+domains.original_effects.capacity()*sizeof(std::uint8_t)+
        domains.captured_inputs.capacity()*sizeof(analysis::ValueKinds);
    auto refined_input=input;refined_input.normal_kinds=domains.expressions;
    TreeCheckBudget expression_budget(remaining(retained+domains_bytes));
    result.expressions=AnalyzeRecoveryExpressions(tree,refined_input,result.initialized_reads,expression_budget);
    charge(retained+domains_bytes,expression_budget.Bytes(),expression_budget.Work());
    std::copy(result.expressions.kinds.begin(),result.expressions.kinds.end(),result.flow.normal_kinds.begin());
    const auto live=retained+domains_bytes+ExpressionBytes(result.expressions);
    auto refined=RefineRecoveryFlowEffects(result.flow,tree,result.expressions.evaluations,
        result.expressions.normal_stores,remaining(live),domains.executable);
    if(const auto* error=std::get_if<TreeStructureError>(&refined))throw *error;
    const auto& refinement=std::get<FlowRefinement>(refined);charge(live,refinement.allocated_bytes,refinement.work);
    result.flow.normal_truths=std::move(domains.truths);
    result.flow.allocated_bytes+=result.flow.normal_truths.capacity()*sizeof(TruthDomains);
    result.flow.retained_bytes+=result.flow.normal_truths.capacity()*sizeof(TruthDomains);
    result.retained_bytes=result.flow.retained_bytes+ExpressionBytes(result.expressions)+
        result.initialized_reads.capacity()*sizeof(std::uint8_t);
    RequireTree(result.retained_bytes<=peak,TreeStructureError::kOwnership);
    result.allocated_bytes=peak;result.work=budget.Work();return result;
  } catch(TreeStructureError error){return error;}
  catch(const std::bad_alloc&){return TreeStructureError::kResourceLimit;}
  catch(const std::length_error&){return TreeStructureError::kResourceLimit;}
}
}  // namespace abcd::source
