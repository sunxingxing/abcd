#include "../../../src/source/source_variables.h"
#include "../../../src/source/recovery_flow.h"
#include "abcd/source/tree_printer.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <iostream>
#include <source_location>
#include <sstream>
using namespace abcd;using namespace abcd::source;
namespace {
void Check(bool good,std::source_location at=std::source_location::current()) {
  if(!good){std::cerr<<"aggregate check "<<at.line()<<'\n';std::abort();}
}
struct Cost {std::uint64_t work;std::size_t bytes;};
Cost Run(unsigned count,bool array,bool reverse,bool emit) {
  FunctionTree tree{};tree.callable=CallableKind::kFunction;tree.strictness=Strictness::kStrict;
  tree.parameter_scope={0};tree.body_scope={1};tree.body={0};
  tree.scopes={{ScopeKind::kParameters,{}},{ScopeKind::kFunctionBody,ScopeId{0}},
      {ScopeKind::kBlock,ScopeId{1}},{ScopeKind::kCatch,ScopeId{1}}};
  tree.names={u"input",u"state",u"error"};tree.origins={OriginalSite{{0},{0}}};
  tree.symbols={{{0},{0},BindingKind::kParameter,DeclarationKind::kParameter,NameEvidence::kMetadata,{}},
      {{1},{1},BindingKind::kCompilerTemporary,DeclarationKind::kLet,NameEvidence::kSynthesized,TemporaryReason::kMultipleUses},
      {{3},{2},BindingKind::kCatchParameter,DeclarationKind::kCatchParameter,NameEvidence::kMetadata,{}}};
  tree.parameters={{{0},{},false}};tree.statements.push_back({BlockStmt{{1},0,0},{0}});
  const auto expr=[&](TreeExpression value){ExprId id{static_cast<unsigned>(tree.expressions.size())};tree.expressions.push_back({std::move(value),{0}});return id;};
  const auto read=[&](unsigned id){return expr(SymbolExpr{{id}});};
  const auto integer=[&](unsigned value){return expr(TreeLiteral{IntegerLiteral{static_cast<std::int32_t>(value)}});};
  const auto stmt=[&](TreeStatement value){StmtId id{static_cast<unsigned>(tree.statements.size())};tree.statements.push_back({std::move(value),{0}});return id;};
  const auto block=[&](ScopeId scope,std::vector<StmtId> children){auto first=static_cast<unsigned>(tree.children.size());tree.children.insert(tree.children.end(),children.begin(),children.end());return stmt(BlockStmt{scope,first,static_cast<unsigned>(children.size())});};
  const auto initial=stmt(DeclarationStmt{{1},integer(0)});
  for(unsigned i=0;i<count;++i) {
    const auto name=array?std::to_string(i):i==0?"__proto__":"field"+std::to_string(i);
    NameId key{static_cast<unsigned>(tree.names.size())};tree.names.emplace_back(name.begin(),name.end());
    const auto saved=expr(AssignmentExpr{SymbolTarget{{1}},integer(i+1)});
    const auto callee=read(0);const auto first=static_cast<unsigned>(tree.arguments.size());tree.arguments.push_back(integer(i));
    const auto call=expr(CallExpr{callee,{first,1},CallReference::kValue});
    tree.properties.push_back({key,expr(SequenceExpr{saved,call}),{0}});
  }
  auto aggregate=expr(ObjectExpr{{0,count},array?AggregateKind::kArray:AggregateKind::kObject});
  const auto attempt=block({2},{stmt(ReturnStmt{aggregate})});
  const auto handler=block({3},{stmt(ReturnStmt{read(1)})});
  const auto tried=stmt(TryStmt{attempt,CatchClause{{2},handler},{}});
  auto& root=std::get<BlockStmt>(tree.statements[0].statement);root.first_child=tree.children.size();root.child_count=2;
  tree.children.insert(tree.children.end(),{initial,tried});
  for(unsigned i=0;i<tree.statements.size();++i)if(const auto* body=std::get_if<BlockStmt>(&tree.statements[i].statement))
    for(unsigned j=0;j<=body->child_count;++j) {
      AnchorId anchor{static_cast<unsigned>(tree.anchors.size())};tree.anchors.push_back({BlockBoundary{{i},j},{0}});
      tree.operations.push_back({{anchor.value},anchor});
    }
  if(reverse) {
    const auto map=[&](ExprId id){return ExprId{static_cast<unsigned>(tree.expressions.size()-1-id.value)};};
    for(auto& item:tree.expressions)std::visit([&](auto& node) {
      using T=std::decay_t<decltype(node)>;
      if constexpr(std::is_same_v<T,AssignmentExpr>)node.value=map(node.value);
      else if constexpr(std::is_same_v<T,SequenceExpr>){node.left=map(node.left);node.right=map(node.right);}
      else if constexpr(std::is_same_v<T,CallExpr>)node.callee=map(node.callee);
      else Check((std::is_same_v<T,ObjectExpr>||std::is_same_v<T,TreeLiteral>||std::is_same_v<T,SymbolExpr>));
    },item.expression);
    for(auto& item:tree.statements)std::visit([&](auto& node) {
      using T=std::decay_t<decltype(node)>;
      if constexpr(std::is_same_v<T,DeclarationStmt>){if(node.initializer)node.initializer=map(*node.initializer);}
      else if constexpr(std::is_same_v<T,ReturnStmt>){if(node.value)node.value=map(*node.value);}
    },item.statement);
    for(auto& property:tree.properties)property.value=map(property.value);
    for(auto& argument:tree.arguments)argument=map(argument);
    aggregate=map(aggregate);std::reverse(tree.expressions.begin(),tree.expressions.end());
  }
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  std::vector<std::optional<SymbolId>> aliases(tree.symbols.size());std::vector<std::optional<PrimitiveId>> literals(tree.symbols.size());
  std::vector<std::optional<ExprId>> replacements(tree.expressions.size());std::vector<std::uint8_t> omitted(tree.statements.size()),normal(tree.expressions.size());
  std::vector<ExpressionEvaluation> evaluations(tree.expressions.size(),ExpressionEvaluation::kRequired);
  LegacyFlowProjection projection{aliases,literals,replacements,omitted,evaluations,normal};
  const auto flow=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,projection));
  Check(flow.aggregates.size()==1&&flow.property_initializers.size()==count);
  const auto& literal=flow.aggregates.front();Check(literal.expression==aggregate&&flow.property_initializers.back()->normal==literal.normal);
  std::vector<std::optional<unsigned>> positions(flow.events.size()),states(flow.events.size());
  auto point=flow.entry;unsigned position=0,state=0;
  for(;;) {
    Check(!positions[point.value]);positions[point.value]=position++;states[point.value]=state;
    const auto& event=flow.events[point.value];
    if(event.definition) {
      const auto& definition=flow.definitions[event.definition->value];
      if(definition.binding==SymbolId{1}&&definition.value) {
        const auto& value=std::get<TreeLiteral>(tree.expressions[definition.value->value].expression);
        state=std::get<IntegerLiteral>(value).value;
      }
    }
    std::optional<FlowPoint> next;
    for(auto edge=event.outgoing;edge;edge=flow.edges[*edge].outgoing)if(flow.edges[*edge].completion==FlowCompletion::kNormal) {
      Check(!next);next=flow.edges[*edge].to;
    }
    if(!next)break;point=*next;
  }
  Check(states[literal.allocation.value]==0);
  auto preceding=*positions[literal.allocation.value];
  for(unsigned i=0;i<count;++i) {
    const auto& property=*flow.property_initializers[i];const auto value=flow.expressions[tree.properties[i].value.value];
    Check(positions[value.value]&&positions[property.before.value]&&preceding<*positions[value.value]&&*positions[value.value]<*positions[property.before.value]);
    Check(states[property.before.value]==i+1);
    Check(flow.events[property.before.value].kind==FlowEventKind::kAggregateDefinition);
    Check(!HasLanguageEffect(flow.events[property.before.value].effects->normal,analysis::LanguageEffect::kUserCode));
    Check(property.exceptions==literal.exceptions);preceding=*positions[property.before.value];
  }
  Check(!HasLanguageEffect(flow.events[literal.allocation.value].effects->normal,analysis::LanguageEffect::kUserCode));
  Check(std::holds_alternative<RecoveryFlow>(BuildRecoveryFlow(tree,projection,{flow.allocated_bytes,flow.work})));
  Check(std::get<TreeStructureError>(BuildRecoveryFlow(tree,projection,{flow.allocated_bytes-1,flow.work}))==TreeStructureError::kResourceLimit);
  Check(std::get<TreeStructureError>(BuildRecoveryFlow(tree,projection,{flow.allocated_bytes,flow.work-1}))==TreeStructureError::kWorkLimit);
  std::ostringstream source;Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree,source)));
  if(emit)std::cout<<nlohmann::json{{"count",count},{"array",array},{"reverse",reverse},{"source",source.str()},{"work",flow.work},{"bytes",flow.allocated_bytes}}<<'\n';
  return {flow.work,flow.allocated_bytes};
}
}
int main(int argc,char**) {
  for(bool array:{false,true})for(bool reverse:{false,true}) {
    Cost previous{};
    for(unsigned count:{1,2,4,200,201,400,800}) {
      const auto cost=Run(count,array,reverse,argc>1);
      if(count>=201){const auto ratio=count==201?1.1:3.0;Check(cost.work<=ratio*previous.work&&cost.bytes<=ratio*previous.bytes);}
      previous=cost;
    }
  }
}
