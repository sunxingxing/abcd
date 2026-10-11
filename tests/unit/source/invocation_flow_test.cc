#include "../../../src/source/recovery_flow.h"
#include "analysis/ssa_fixture.h"
using namespace abcd;using namespace abcd::source;
namespace {
std::pair<std::uint64_t,std::size_t> Run(unsigned count,bool reverse) {
  FunctionTree tree{};tree.callable=CallableKind::kFunction;tree.strictness=Strictness::kStrict;
  tree.parameter_scope={0};tree.body_scope={1};tree.body={0};
  tree.scopes={{ScopeKind::kParameters,{}},{ScopeKind::kFunctionBody,ScopeId{0}}};
  tree.origins={OriginalSite{{0},{0}}};
  for(unsigned i=0;i<count;++i) {
    const auto text=std::to_string(i);tree.names.push_back(u"arg"+std::u16string(text.begin(),text.end()));
    tree.symbols.push_back({{0},{i},BindingKind::kParameter,DeclarationKind::kParameter,NameEvidence::kMetadata,{}});
    tree.parameters.push_back({{reverse?count-i-1:i},{},i+1==count});
  }
  tree.expressions={{{SymbolExpr{tree.parameters.back().symbol}},{0}}};
  tree.statements={{{BlockStmt{{1},0,1}},{0}},{{ReturnStmt{ExprId{0}}},{0}}};tree.children={{1}};
  std::vector<std::optional<SymbolId>> aliases(count);
  std::vector<std::optional<PrimitiveId>> literals(count);
  std::vector<std::optional<ExprId>> replacements(1);
  std::vector<std::uint8_t> omitted(2),normal(1);
  std::vector<ExpressionEvaluation> effects(1,ExpressionEvaluation::kRequired);
  const LegacyFlowProjection projection{aliases,literals,replacements,omitted,effects,normal};
  auto flow=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,projection));
  test::Check(flow.invocation_values.size()==1&&flow.original_invocations.empty());
  const auto& value=flow.invocation_values.front();test::Check(value.parameter==ParameterId{count-1});
  test::Check(flow.definitions[value.definition.value].binding==tree.parameters.back().symbol);
  test::Check(flow.definitions[value.definition.value].kind==FlowDefinitionKind::kRestParameter);
  test::Check(std::holds_alternative<RecoveryFlow>(BuildRecoveryFlow(tree,projection,{flow.allocated_bytes,flow.work})));
  test::Check(std::get<TreeStructureError>(BuildRecoveryFlow(tree,projection,{flow.allocated_bytes-1,flow.work}))==TreeStructureError::kResourceLimit);
  test::Check(std::get<TreeStructureError>(BuildRecoveryFlow(tree,projection,{flow.allocated_bytes,flow.work-1}))==TreeStructureError::kWorkLimit);
  const auto cost=std::pair{flow.work,flow.allocated_bytes};
  tree.parameters.back().rest=false;
  flow=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,projection,{},std::move(flow)));
  test::Check(flow.invocation_values.empty());
  for(const auto& definition:flow.definitions)test::Check(definition.kind!=FlowDefinitionKind::kRestParameter);
  std::cout<<count<<' '<<reverse<<' '<<cost.first<<' '<<cost.second<<'\n';return cost;
}
}
int main() {
  for(bool reverse:{false,true}) {
    std::pair<std::uint64_t,std::size_t> previous{};
    for(unsigned count:{1U,2U,200U,201U,400U,800U}) {
      const auto cost=Run(count,reverse);
      if(count>200){test::Check(cost.first<previous.first*3);test::Check(cost.second<previous.second*3);}previous=cost;
    }
  }
}
