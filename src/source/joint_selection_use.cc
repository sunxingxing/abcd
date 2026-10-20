#include "joint_selection_use.h"
#include "evaluation_plan.h"
#include "evaluation_aliases.h"
#include "logical_evaluation.h"
#include "call_plan.h"
#include "tree_checks.h"
namespace abcd::source {
namespace {
using namespace analysis;
bool Pure(const ValueOp& op) {
  return op.normal==LanguageEffect::kNone && op.before_throw==LanguageEffect::kNone &&
      op.exceptional==ExceptionalCompletion::kImpossible;
}
bool PredicateInput(const ValueProgram& graph,const EvaluationPlan& plan,const OperandUse& use) {
  const auto* owner=std::get_if<OpId>(&use.owner); if(!owner) return false;
  const auto& op=graph.ops[owner->value];
  if(!Pure(op)) return false;
  const auto placement=BeforeLogicalFusion(graph,plan,owner->value);
  if(placement.kind!=EvaluationKind::kInline || !placement.consumer) return false;
  if(const auto* unary=std::get_if<ValueUnary>(&op.operation))
    return unary->op==UnaryOperator::kToBoolean || unary->op==UnaryOperator::kLogicalNot;
  const auto* binary=std::get_if<ValueBinary>(&op.operation);
  if(!binary || op.use_count!=2 || (binary->op!=BinaryOperator::kStrictEqual && binary->op!=BinaryOperator::kStrictNotEqual)) return false;
  const auto other=op.first_use.value+(static_cast<std::size_t>(&use-graph.uses.data())==op.first_use.value?1:0);
  const auto operand=ResolveEvaluationOperand(plan,graph.uses[other].operand);
  const auto* constant=std::get_if<SpecialValue>(&operand);
  return constant && (*constant==SpecialValue::kNull || *constant==SpecialValue::kUndefined);
}
}
std::vector<JointSelectionUse> FindJointSelectionUses(const ValueProgram& graph,
    const EvaluationPlan& plan,TreeCheckBudget& budget) {
  using namespace analysis;
  struct Uses {std::optional<UseId> only,keep;std::size_t predicates=0;bool other=false,invalid=false;};
  std::vector<Uses> uses;
  std::vector<std::optional<ValueId>> parents;
  std::vector<std::optional<OpId>> roots;
  std::vector<ValueId> path;
  enum class Visit : std::uint8_t {kUnseen,kActive,kDone};
  std::vector<Visit> visited;
  budget.Array(uses,graph.values.size()); budget.Array(parents,graph.values.size());
  budget.Array(roots,graph.values.size()); budget.Array(path,graph.values.size());budget.Array(visited,graph.values.size());
  for(std::size_t i=0;i<graph.uses.size();++i) {
    budget.Step();const auto& use=graph.uses[i];if(use.life!=ValueLife::kLive || IsIdentityPhiInput(plan,use))continue;
    if(const auto* owner=std::get_if<OpId>(&use.owner)) {
      const auto kind=plan.operations[owner->value].kind;
      if(kind==EvaluationKind::kAlias || kind==EvaluationKind::kElidedPure)continue;
    }
    const auto input=ResolveEvaluationOperand(plan,use.operand);const auto* value=std::get_if<ValueId>(&input);if(!value)continue;
    auto& set=uses[value->value];if(set.only)set.other=true;else set.only=UseId{static_cast<std::uint32_t>(i)};
    if(std::holds_alternative<ValueId>(use.owner) && use.role==OperandRole::kPhiInput && use.incoming && std::holds_alternative<EdgeId>(*use.incoming)) {
      if(set.keep)set.invalid=true;else set.keep=UseId{static_cast<std::uint32_t>(i)};
    } else if(PredicateInput(graph,plan,use)) {++set.predicates;}
    else set.invalid=true;
  }
  for(std::size_t i=0;i<uses.size();++i) {
    budget.Step();const auto& set=uses[i];
    const bool joint=std::holds_alternative<PhiDefinition>(graph.values[i].origin) && set.keep && set.predicates && !set.invalid;
    const auto use=joint?set.keep:!set.other?set.only:std::nullopt;if(!use)continue;
    const auto& occurrence=graph.uses[use->value];
    if(const auto* phi=std::get_if<ValueId>(&occurrence.owner)) parents[i]=*phi;
    else {
      const auto owner=std::get<OpId>(occurrence.owner);
      if(const auto property=MemberProperty(graph,plan,owner);property && BeforeLogicalFusion(graph,plan,property->value).kind==EvaluationKind::kRoot && BeforeLogicalFusion(graph,plan,property->value).temporary==TemporaryReason::kCrossRegionValue) roots[i]=owner;
      else if(graph.ops[owner.value].result && BeforeLogicalFusion(graph,plan,owner.value).kind==EvaluationKind::kInline)
        parents[i]=graph.ops[owner.value].result;
    }
  }
  // Compress the actual value-use forest, including phi keep edges. Cycles do
  // not prove a call owner. Every edge is visited at most once.
  for(std::size_t i=0;i<parents.size();++i) {
    budget.Step();if(visited[i]==Visit::kDone)continue;
    ValueId at{static_cast<std::uint32_t>(i)};std::size_t count=0;
    while(visited[at.value]==Visit::kUnseen) {
      budget.Step();visited[at.value]=Visit::kActive;path[count++]=at;
      if(!parents[at.value])break;at=*parents[at.value];
    }
    const auto root=(!parents[at.value] || visited[at.value]==Visit::kDone)?roots[at.value]:std::nullopt;
    while(count){budget.Step();const auto id=path[--count];roots[id.value]=root;visited[id.value]=Visit::kDone;}
  }
  std::vector<JointSelectionUse> result;
  budget.Admit(graph.values.size(),sizeof(JointSelectionUse));result.reserve(graph.values.size());
  budget.Admit(result.capacity()-graph.values.size(),sizeof(JointSelectionUse));
  std::vector<std::optional<std::size_t>> indices;budget.Array(indices,graph.values.size());
  for(std::size_t i=0;i<uses.size();++i) {
    budget.Step();const auto& set=uses[i];
    if(!roots[i] || !std::holds_alternative<PhiDefinition>(graph.values[i].origin) || !set.keep || !set.predicates || set.invalid)continue;
    indices[i]=result.size();result.push_back({ValueId{static_cast<std::uint32_t>(i)},*set.keep,*roots[i],{}});
    auto& predicates=result.back().predicates;budget.Admit(set.predicates,sizeof(UseId));predicates.reserve(set.predicates);
    budget.Admit(predicates.capacity()-set.predicates,sizeof(UseId));
  }
  for(std::size_t i=0;i<graph.uses.size();++i) {
    budget.Step();const auto& use=graph.uses[i];if(use.life!=ValueLife::kLive)continue;
    const auto input=ResolveEvaluationOperand(plan,use.operand);const auto* value=std::get_if<ValueId>(&input);
    if(value && indices[value->value] && PredicateInput(graph,plan,use))result[*indices[value->value]].predicates.push_back(UseId{static_cast<std::uint32_t>(i)});
  }
  return result;
}
void VerifyJointSelectionUses(const analysis::ValueProgram& graph,const EvaluationPlan& plan,
    std::span<const JointSelectionUse> proofs,TreeCheckBudget& budget) {
  if(proofs.empty())return;
  const auto fresh=FindJointSelectionUses(graph,plan,budget);
  std::size_t next=0;
  for(const auto& proof:proofs) {
    budget.Step(1+proof.predicates.size());while(next<fresh.size() && fresh[next].value<proof.value){budget.Step();++next;}
    RequireTree(next<fresh.size() && fresh[next]==proof,TreeStructureError::kOwnership);++next;
  }
}
}
