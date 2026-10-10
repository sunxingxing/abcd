#ifndef ABCD_TEST_SUPPORT_PURE_CONTROL_ORACLE_H_
#define ABCD_TEST_SUPPORT_PURE_CONTROL_ORACLE_H_
#include "ssa_fixture.h"
#include "../../../src/source/recovery_flow.h"
#include <array>
namespace abcd::test {
// Small, concrete CFG interpreter for the decoded pure-selection fixtures.
// It reads SSA operands and branches, never the published domain equations.
// The captured-input producer supplies a chosen representative value. These
// hypothetical inputs check the test's function/path, including domains that
// a particular producer cannot return; they do not certify producer behavior.
// Callback effects and whole functions have separate independent JS tests.
using PureValue=std::variant<analysis::SpecialValue,std::int32_t>;
inline bool PureBoolean(PureValue value) {
  if(const auto* number=std::get_if<std::int32_t>(&value))return *number!=0;
  return std::get<analysis::SpecialValue>(value)==analysis::SpecialValue::kTrue;
}
inline bool PurePredicate(analysis::UnaryOperator operation,PureValue value) {
  using U=analysis::UnaryOperator;using S=analysis::SpecialValue;
  switch(operation) {
    case U::kToBoolean:return PureBoolean(value);
    case U::kLogicalNot:return !PureBoolean(value);
    case U::kFalseOrZero:return value==PureValue{S::kFalse}||value==PureValue{std::int32_t{0}};
    case U::kTrueOrNonzeroNumber:return value==PureValue{S::kTrue}||
        (std::holds_alternative<std::int32_t>(value)&&std::get<std::int32_t>(value)!=0);
    default:Check(false);return false;
  }
}
inline std::vector<std::optional<bool>> ConcretePureControls(const analysis::ValueProgram& graph,PureValue input) {
  using namespace analysis;
  std::vector<std::optional<PureValue>> values(graph.values.size());
  for(unsigned i=0;i<values.size();++i)if(std::holds_alternative<EntryDefinition>(graph.values[i].origin))values[i]=input;
  const auto operand=[&](const ValueOperand& value) -> PureValue {
    if(const auto* id=std::get_if<ValueId>(&value)){Check(values[id->value].has_value());return *values[id->value];}
    if(const auto* special=std::get_if<SpecialValue>(&value))return *special;
    if(const auto* number=std::get_if<analysis::IntegerLiteral>(&value))return number->value;
    Check(false);return input;
  };
  std::vector<std::optional<bool>> tests(graph.ops.size());std::vector<bool> visited(graph.blocks.size());
  WorkBlockId current{0};
  for(;;) {
    Check(current.value<visited.size()&&!visited[current.value]);visited[current.value]=true;
    const auto& block=graph.blocks[current.value];std::optional<bool> taken;
    for(unsigned j=0;j<block.op_count;++j) {
      const OpId id{block.first_op.value+j};const auto& op=graph.ops[id.value];if(op.life!=ValueLife::kLive)continue;
      const auto use=[&](unsigned at){Check(at<op.use_count);return operand(graph.uses[op.first_use.value+at].operand);};
      std::optional<PureValue> result;
      if(std::holds_alternative<ValueCopy>(op.operation)){result=use(0);tests[id.value]=PureBoolean(*result);}
      else if(const auto* unary=std::get_if<ValueUnary>(&op.operation)) {
        // Numeric continuation follows the selection in one fixture; its
        // value is irrelevant to this oracle, which stops at that boundary.
        if(unary->op==UnaryOperator::kToNumber)return tests;
        const bool value=PurePredicate(unary->op,use(0));tests[id.value]=value;
        result=value?SpecialValue::kTrue:SpecialValue::kFalse;
      } else if(const auto* binary=std::get_if<ValueBinary>(&op.operation)) {
        Check(binary->op==BinaryOperator::kStrictEqual||binary->op==BinaryOperator::kStrictNotEqual);
        bool value=use(0)==use(1);if(binary->op==BinaryOperator::kStrictNotEqual)value=!value;
        tests[id.value]=value;result=value?SpecialValue::kTrue:SpecialValue::kFalse;
      } else if(const auto* branch=std::get_if<ValueBranch>(&op.operation))taken=tests[id.value]=PurePredicate(branch->predicate,use(0));
      else if(std::holds_alternative<ValueNamedRead>(op.operation)||std::holds_alternative<ValueInvoke>(op.operation)||
              std::holds_alternative<ValueArrayAllocation>(op.operation))result=input;
      else if(std::holds_alternative<ValueReturn>(op.operation))return tests;
      else if(!std::holds_alternative<ValueJump>(op.operation)) {
        std::cerr<<"pure oracle unsupported variant="<<op.operation.index()<<'\n';Check(false);
      }
      if(result&&op.result)values[op.result->value]=*result;
    }
    std::optional<WorkBlockId> next;
    for(unsigned j=0;j<block.edge_count;++j) {
      const auto& edge=graph.edges[block.first_edge.value+j];
      if(!taken||(edge.kind==FlowEdgeKind::kTaken)==*taken){Check(!next);next=edge.target;}
    }
    if(!next)return tests;current=*next;
  }
}
inline void CheckPureControls(const analysis::ValueProgram& graph,const source::RecoveryFlow& flow) {
  using namespace source;using S=analysis::SpecialValue;
  const std::array<PureValue,4> representatives{S::kNull,S::kUndefined,S::kFalse,S::kTrue};
  std::array<std::vector<std::optional<bool>>,4> concrete;
  for(unsigned i=0;i<concrete.size();++i)concrete[i]=ConcretePureControls(graph,representatives[i]);
  unsigned checked=0;
  for(unsigned i=0;i<flow.original->operations.size();++i) {
    const auto current=FindFlowOriginalControl(flow,analysis::OpId{i});if(!current||!current->test)continue;
    ++checked;TruthDomains evaluated,true_values;
    for(unsigned d=0;d<concrete.size();++d)if(concrete[d][i]) {
      const auto domain=TruthDomains::One(static_cast<TruthDomain>(d));evaluated|=domain;
      if(*concrete[d][i])true_values|=domain;
    }
    Check(current->test->evaluated==evaluated);Check(current->test->true_values==true_values);
    // Exhaust every small input domain, independently joining concrete paths.
    for(unsigned mask=0;mask<(1U<<concrete.size());++mask) {
      TruthDomains input;for(unsigned d=0;d<concrete.size();++d)if(mask&(1U<<d))input|=TruthDomains::One(static_cast<TruthDomain>(d));
      auto truths=flow.normal_truths;truths[current->expression.value]=input;
      const auto actual=OriginalControlDomains(flow,analysis::OpId{i},truths);Check(actual.has_value());
      Check(actual->evaluated==(input&evaluated));Check(actual->true_values==(input&true_values));
      Check(actual->false_values==(input&evaluated&true_values.Complement()));
      const auto result=OriginalControlResultDomains(flow,analysis::OpId{i},truths);
      if(current->test->outcome==PureTruthOutcome::kBoolean&&!actual->evaluated.Empty()) {
        Check(result.kinds.IsBoolean());TruthDomains expected;
        if(!actual->true_values.Empty())expected|=TruthDomains::Truthy();
        if(!actual->false_values.Empty())expected|=TruthDomains::One(TruthDomain::kFalsy);
        Check(result.truth==expected);
      } else if(current->test->outcome==PureTruthOutcome::kDecision||actual->evaluated.Empty())Check(result.kinds.IsEmpty());
    }
  }
  Check(checked>0);
}
}
#endif
