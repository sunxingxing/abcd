#ifndef ABCD_TEST_SOURCE_PROJECTED_RANGES_H_
#define ABCD_TEST_SOURCE_PROJECTED_RANGES_H_
#include "../../../src/source/binding_initializer.h"
#include "../../../src/source/expression_children.h"
#include "../../../src/source/statement_expressions.h"
#include "abcd/source/tree_printer.h"
#include "../../support/analysis/ssa_fixture.h"
namespace abcd::test {
// Source origins of removed owned syntax map to an explicit empty boundary.
// Forwarded values remain at their retained destination. This checks range
// ownership only; independent integration execution proves transformation safety.
inline void CheckProjectedRanges(const source::FunctionTree& tree,
    const source::BindingInitializers& plan,const source::PrintedFunction& printed,
    std::size_t output_size) {
  std::vector<std::uint8_t> removed(tree.expressions.size());
  std::vector<source::ExprId> pending;
  for(std::size_t i=0;i<tree.statements.size();++i) {
    const auto& completion=plan.declaration_completions[i];
    const auto& value=plan.declaration_values[i];
    if(!plan.omitted[i]&&!completion&&!value)continue;
    const auto retained=plan.omitted[i]?plan.forwarded_values[i]:completion?completion->evaluation:
        value?std::optional{value->value}:std::nullopt;
    source::VisitStatementExpressions(tree.statements[i].statement,[&](auto root){pending.push_back(root);});
    while(!pending.empty()) {
      const auto id=pending.back();pending.pop_back();
      if(retained==id)continue;
      removed[id.value]=1;
      source::VisitExpressionChildren(tree,tree.expressions[id.value].expression,[&](auto child){
        // A transported RHS remains at its read destination. Removing its
        // original wrapper cannot turn that retained subtree into a boundary.
        if(const auto transport=plan.transports[id.value];transport&&transport->value==child)return;
        pending.push_back(child);
      });
    }
  }
  Check(printed.expressions.size()==tree.expressions.size());
  for(std::size_t i=0;i<printed.expressions.size();++i) {
    const auto range=printed.expressions[i];
    Check(range.begin<=range.end&&range.end<=output_size);
    Check((range.begin==range.end)==bool(removed[i]));
    if(removed[i])Check(printed.expression_extents[i]==source::GeneratedExtent::kBoundary);
  }
}
}
#endif
