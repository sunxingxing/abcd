#ifndef ABCD_TEST_SOURCE_PROTOCOL_VALUES_H_
#define ABCD_TEST_SOURCE_PROTOCOL_VALUES_H_
#include "../../../src/source/source_variables.h"
#include "../../../src/source/recovery_projection.h"
#include <cstdlib>
#include <iostream>
#include <source_location>
namespace abcd::test {
inline void ProtocolValueCheck(bool good,std::source_location at=std::source_location::current()) {
  if(!good){std::cerr<<"protocol value check "<<at.line()<<'\n';std::abort();}
}
// Supply the independently decoded operation inventory. A missing adapter
// marker cannot silently remove an entire category from this check.
inline void CheckProtocolValues(const source::FunctionTree& tree,const source::OriginalFlow& original,
    std::span<const analysis::OpId> results) {
  using namespace source;using namespace analysis;
  std::vector<FlowValueEquation> equations(tree.expressions.size());
  std::vector<ExprId> order;
  for(unsigned i=0;i<tree.expressions.size();++i) {
    order.push_back({i});
    if(const auto* read=std::get_if<SymbolExpr>(&tree.expressions[i].expression))equations[i]=FlowBindingValue{read->symbol};
  }
  const auto variables=std::get<SourceVariables>(PrepareSourceVariables(tree,{},&original));
  const auto& plan=variables.bindings;
  LegacyFlowProjection projection{plan.aliases,plan.literals,plan.replacements,plan.omitted,{},{}};
  projection.loops=&variables.loops;
  const auto check=[&](const OriginalFlow* raw,const BindingInitializers& candidate,bool replaced=false) {
    const RecoveryExpressionInput input{candidate,equations,order,raw};
    const auto model=std::get<RecoveryProjection>(PrepareRecoveryProjection(tree,projection,input));
    for(auto id:results) {
      const auto& operation=original.operations[id.value];
      ProtocolValueCheck(operation.expression.has_value());
      const auto expression=*operation.expression;
      const auto& node=tree.expressions[expression.value].expression;
      // A VM Hole becomes a private object sentinel. The source value cannot
      // inherit an internal VM tag as a narrowing certificate.
      ProtocolValueCheck(model.expressions.kinds[expression.value].IsLanguage());
      if(replaced) {
        // A retained conditional correspondence is not a consumed range.
        // The joint solver must honor the candidate's language value even
        // when the static original table still names this source occurrence.
        ProtocolValueCheck(model.expressions.kinds[expression.value]==ValueKinds::Language());
      } else if(const auto* read=std::get_if<SymbolExpr>(&node)) {
        ProtocolValueCheck(tree.hole==read->symbol);
        ProtocolValueCheck(!model.expressions.kinds[expression.value].IsEmpty());
      } else {
        const auto& object=std::get<ObjectExpr>(node);
        const FlowAggregate* aggregate=nullptr;
        for(const auto& candidate:model.flow.aggregates)if(candidate.expression==expression)aggregate=&candidate;
        ProtocolValueCheck(aggregate&&aggregate->properties.first==object.properties.first&&
            aggregate->properties.count==object.properties.count);
        ProtocolValueCheck(aggregate->allocation!=aggregate->allocated&&aggregate->allocated!=aggregate->normal);
        for(unsigned i=0;i<object.properties.count;++i)
          ProtocolValueCheck(model.flow.property_initializers[object.properties.first+i].has_value());
      }
      if(raw)ProtocolValueCheck(!model.flow.original_actions[id.value]);
    }
  };
  check(&original,plan);check(nullptr,plan);
  auto missing=original;
  for(auto id:results)missing.operations[id.value].result_expression.reset();
  check(&missing,plan);
  auto candidate=plan;const PrimitiveId undefined{static_cast<unsigned>(candidate.primitives.size())};
  candidate.primitives.push_back(UndefinedLiteral{});
  for(auto id:results) {
    const auto expression=*original.operations[id.value].expression;
    candidate.transports[expression.value]=ExpressionTransport{expression,undefined};
  }
  check(&original,candidate,true);
}
}
#endif
