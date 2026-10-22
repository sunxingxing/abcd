#include "../../../src/source/family_print.h"
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <iostream>
#include <sstream>
using namespace abcd;
using namespace abcd::source;
namespace {
void Check(bool ok) { if (!ok) std::abort(); }
FunctionTree Base(unsigned owner) {
  FunctionTree t{};
  t.owner={owner}; t.callable=CallableKind::kFunction; t.strictness=Strictness::kStrict;
  t.parameter_scope={0}; t.body_scope={1}; t.body={0};
  t.scopes={{ScopeKind::kParameters,{}},{ScopeKind::kFunctionBody,ScopeId{0}}};
  t.origins={EntryOrigin{}};
  t.statements={{BlockStmt{{1},0,1},{0}},{ReturnStmt{ExprId{0}},{0}}}; t.children={{1}};
  return t;
}
ConstructedFamily Make(CallableKind kind,unsigned place) {
  auto root=Base(0), child=Base(1);
  root.names={u"f",u"key",u"chosen",u"name"};
  root.symbols={{{0},{0},BindingKind::kParameter,DeclarationKind::kParameter,NameEvidence::kSynthesized,{}}};
  root.parameters={{{0},{},false}};
  auto add=[&](TreeExpression e) { ExprId id{static_cast<std::uint32_t>(root.expressions.size())}; root.expressions.push_back({std::move(e),{0}}); return id; };
  auto result=add(FunctionExpr{{1},{0,0}});
  if(place==1) { root.arguments={result}; result=add(CallExpr{add(SymbolExpr{{0}}),{0,1},CallReference::kValue}); }
  if(place==2) { root.arguments={result}; result=add(ObjectExpr{{0,1},AggregateKind::kArray}); root.names.push_back(u"0"); root.properties={{NameId{4},{0},{0}}}; root.arguments.clear(); }
  if(place==3) { root.properties={{NameId{1},result,{0}}}; result=add(ObjectExpr{{0,1}}); }
  if(place==4) {
    root.symbols.push_back({{1},{2},BindingKind::kLocal,DeclarationKind::kLet,NameEvidence::kSynthesized,{}});
    root.statements.push_back({DeclarationStmt{{1},result},{0}}); root.children.insert(root.children.begin(),{2});
    std::get<BlockStmt>(root.statements[0].statement).child_count=2; result=add(SymbolExpr{{1}});
  }
  if(place==5) result=add(ConditionalExpr{add(SymbolExpr{{0}}),result,add(TreeLiteral{UndefinedLiteral{}})});
  if(place==6) result=add(MemberExpr{result,NameId{3}});
  if(place==7) {
    root.statements.push_back({ExpressionStmt{result},{0}}); root.children.insert(root.children.begin(),{2});
    std::get<BlockStmt>(root.statements[0].statement).child_count=2; result=add(TreeLiteral{IntegerLiteral{7}});
  }
  root.statements[1].statement=ReturnStmt{result};
  child.callable=kind; child.names={u"x",place==3?u"key":place==4?u"chosen":u""}; child.inferred_name=NameId{1};
  child.symbols={{{0},{0},BindingKind::kParameter,DeclarationKind::kParameter,NameEvidence::kSynthesized,{}}};
  child.parameters={{{0},{},false}}; child.expressions={{SymbolExpr{{0}},{0}}};
  ConstructedFamily family; family.root={0}; family.members={{std::move(root),{},{}},{std::move(child),{},{}}}; return family;
}
}
int main(int argc,char**) {
  for(auto kind:{CallableKind::kFunction,CallableKind::kArrow,CallableKind::kAsyncFunction,CallableKind::kAsyncArrow})
    for(unsigned place=0;place<8;++place) {
      const auto family=Make(kind,place); std::ostringstream output;
      auto printed=PrintFamilyDraft(family,output); Check(std::holds_alternative<PrintedFamilyDraft>(printed));
      const auto& spans=std::get<PrintedFamilyDraft>(printed);
      for(const auto& member:spans.members) for(const auto& span:member.expressions)
        Check(span.begin<=span.end && span.end<=output.str().size());
      if(place==6) Check(output.str().find(").name")!=std::string::npos);
      if(place==1) Check(output.str().find("return f(")!=std::string::npos);
      if(argc>1) std::cout<<nlohmann::json{{"place",place},{"source",output.str()}}.dump()<<'\n';
    }
}
