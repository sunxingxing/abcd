#include "abcd/source/tree_printer.h"
#include "abcd/source/counting_sink.h"
#include <sstream>
#include <iostream>
#include <cstdlib>
using namespace abcd;using namespace abcd::source;
namespace {
void Check(bool x){if(!x)std::abort();}
FunctionTree Make(std::string_view form,unsigned count=1){
 FunctionTree t{};t.owner={0};t.callable=CallableKind::kFunction;t.strictness=Strictness::kStrict;
 t.parameter_scope={0};t.body_scope={1};t.body={0};
 t.scopes={{ScopeKind::kParameters,{}},{ScopeKind::kFunctionBody,ScopeId{0}}};t.origins={EntryOrigin{}};
 t.names={u"f",u"flag",u"x",u"p",u"push"};
 for(unsigned i=0;i<3;++i){t.symbols.push_back({{0},{i},BindingKind::kParameter,DeclarationKind::kParameter,NameEvidence::kSynthesized,{}});t.parameters.push_back({{i},{},false});}
 auto add=[&](TreeExpression e){ExprId id{static_cast<std::uint32_t>(t.expressions.size())};t.expressions.push_back({std::move(e),{0}});return id;};
 auto read=[&](unsigned i){return add(SymbolExpr{{i}});};
 auto callee=read(0);auto reference=CallReference::kValue;
 if(form=="member"||form=="base"){
   auto base=callee;
   if(form=="base") {t.expressions.pop_back();base=add(AssignmentExpr{SymbolTarget{{2}},add(EmptyArrayExpr{})});}
   callee=add(MemberExpr{base,NameId{4}});reference=CallReference::kMember;
 }
 for(unsigned i=0;i<count;++i){
   auto update=add(UpdateExpr{{2},UpdateDirection::kIncrement,
       form=="sign" ? UpdateResult::kPrefix : UpdateResult::kPostfix});
   if(form=="sign") update=add(UnaryExpr{TreeUnary::kPlus,update});
   if(form=="updatebase") update=add(MemberExpr{update,NameId{3}});
   const auto property=static_cast<std::uint32_t>(t.properties.size());t.properties.push_back({{3},update,{0}});
   const auto object=add(ObjectExpr{{property,1}});
   const auto plus=add(BinaryExpr{analysis::BinaryOperator::kAdd,read(2),add(TreeLiteral{IntegerLiteral{1}})});
   const auto assignment=add(AssignmentExpr{SymbolTarget{{2}},plus});
   const auto sequence=add(SequenceExpr{assignment,add(EmptyObjectExpr{})});
   const auto chosen=add(ConditionalExpr{read(1),object,sequence});
   t.arguments.push_back(chosen);
 }
 const auto result=add(CallExpr{callee,{0,count},reference});
 t.statements={{BlockStmt{{1},0,1},{0}},{ReturnStmt{result},{0}}};t.children={{1}};return t;
}
std::string Print(const FunctionTree& t){
 std::ostringstream out;const auto result=PrintFunctionTree(t,out);Check(std::holds_alternative<PrintedFunction>(result));
 const auto& positions=std::get<PrintedFunction>(result);Check(positions.bytes==out.str().size());
 for(const auto& span:positions.expressions)Check(span.begin<=span.end&&span.end<=positions.bytes);
 CountingSink sink;std::ostream counted(&sink);const auto count=PrintFunctionTree(t,counted);Check(std::holds_alternative<PrintedFunction>(count));Check(sink.Bytes()==positions.bytes);
 return out.str();
}
}
int main(int argc,char** argv){
 if(argc==2){std::cout<<Print(Make(argv[1]));return 0;}
 for(auto form:{"value","member","base","sign","updatebase"}){
   auto text=Print(Make(form));
   if(std::string_view(form)=="sign") Check(text.find("+(++x)")!=std::string::npos);
   else if(std::string_view(form)=="updatebase") Check(text.find("(x++).p")!=std::string::npos);
   else Check(text.find("flag ? {p: x++}")!=std::string::npos);
   Check(text.find("x = x + 1, {}")!=std::string::npos);
   if(std::string_view(form)=="base")Check(text.find("(x = []).push(")!=std::string::npos);
 }
 for(auto n:{200u,201u,400u,800u}){auto text=Print(Make("value",n));Check(!text.empty());}
}
