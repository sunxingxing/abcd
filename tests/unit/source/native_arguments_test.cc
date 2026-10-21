#include "../../../src/source/native_arguments.h"
#include <cstdlib>
#include <iostream>
using namespace abcd;
using namespace abcd::source;
using namespace abcd::analysis;
namespace {
void Check(bool value){if(!value)std::abort();}
ValueProgram Make(std::uint32_t count) {
 ValueProgram g;g.arguments=ArgumentFrameFact{};g.arguments->frame.parameters=count;
 g.blocks.push_back({OpId{0},1,EdgeId{0},0});
 for(std::uint32_t i=0;i<count;++i){g.input.values.push_back(ValueId{i});g.values.push_back({EntryDefinition{ParameterEntry{i}},ValueKinds{},UseId{i}});g.uses.push_back({OpId{0},OperandRole::kArgument,ReadPurpose::kProtocol,ValueId{i}});}
 g.ops.push_back({ValueArgumentSnapshot{count},WorkBlockId{0},0,UseId{0},count,{},LanguageEffect::kAllocation,LanguageEffect::kNone,ExceptionalCompletion::kMayThrow,OriginId{0}});
 return g;
}
bool Proves(const ValueProgram& g,SemanticBudget budget={}){TreeCheckBudget b(budget);return std::holds_alternative<NativeArgumentsProof>(ProveNativeArguments(g,CallableKind::kFunction,b));}
}
int main(){
 {
  FunctionTree tree{};tree.identity.contract=SourceContract::kLanguageSourceV6;
  tree.callable=CallableKind::kFunction;tree.strictness=Strictness::kStrict;
  tree.parameter_scope={0};tree.body_scope={1};tree.body={0};
  tree.scopes={{ScopeKind::kParameters,{}},{ScopeKind::kFunctionBody,ScopeId{0}}};
  tree.origins={OriginalSite{{0},{0}}};
  tree.expressions={{{ArgumentSnapshotExpr{tree.owner,{0,0},ArgumentSnapshotForm::kNativeEntry}},{0}}};
  tree.statements={{{BlockStmt{{1},0,1}},{0}},{{ReturnStmt{ExprId{0}}},{0}}};tree.children={{1}};
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  auto wrong=tree;wrong.strictness=Strictness::kSloppy;Check(std::holds_alternative<TreeStructureError>(VerifyTreeStructure(wrong)));
  wrong=tree;wrong.callable=CallableKind::kArrow;Check(std::holds_alternative<TreeStructureError>(VerifyTreeStructure(wrong)));
  wrong=tree;wrong.identity.contract=SourceContract::kLanguageSourceV5;Check(std::holds_alternative<TreeStructureError>(VerifyTreeStructure(wrong)));
  wrong=tree;std::get<ArgumentSnapshotExpr>(wrong.expressions[0].expression).owner.value=1;Check(std::holds_alternative<TreeStructureError>(VerifyTreeStructure(wrong)));
 }

 {
  auto copy=Make(1);copy.uses.push_back({OpId{0},OperandRole::kInput,ReadPurpose::kOperand,ValueId{0}});
  copy.ops.insert(copy.ops.begin(),ValueOp{ValueCopy{},WorkBlockId{0},0,UseId{1},1,ValueId{1},LanguageEffect::kNone,LanguageEffect::kNone,ExceptionalCompletion::kImpossible,OriginId{0}});
  copy.values.push_back({OpResult{OpId{0},{}},ValueKinds{},UseId{0}});copy.uses[0].owner=OpId{1};copy.uses[0].operand=ValueId{1};copy.blocks[0].op_count=2;
  Check(Proves(copy));
  auto split=copy;split.blocks[0].op_count=1;split.blocks[0].edge_count=1;split.blocks.push_back({OpId{1},1,EdgeId{1},0});split.edges.push_back({WorkBlockId{0},WorkBlockId{1},FlowEdgeKind::kFallthrough,0});split.ops[1].block=WorkBlockId{1};Check(Proves(split));
  split.edges.push_back({WorkBlockId{1},WorkBlockId{0},FlowEdgeKind::kFallthrough,0});split.blocks[1].edge_count=1;Check(!Proves(split));
  auto wrong=copy;wrong.arguments.reset();Check(!Proves(wrong));
 }
 {
  FunctionTree shadow{};shadow.names={u"arguments"};shadow.symbols={{{0},{0},BindingKind::kCapture,DeclarationKind::kLet,NameEvidence::kMetadata,{}}};
  TreeCheckBudget budget({});bool refused=false;try{VerifyNativeArgumentsNames(shadow,budget);}catch(TreeStructureError error){refused=error==TreeStructureError::kBinding;}Check(refused);
 }
 Check(NativeArguments(SourceContract::kLanguageSourceV6));Check(!NativeArguments(SourceContract::kLanguageSourceV5));
 for(auto count:{0U,200U,201U,400U,800U}){
  auto g=Make(count);TreeCheckBudget b({});auto p=ProveNativeArguments(g,CallableKind::kFunction,b);Check(std::holds_alternative<NativeArgumentsProof>(p));auto proof=std::get<NativeArgumentsProof>(p);Check(VerifyNativeArguments(g,CallableKind::kFunction,proof,b));
  auto bad=proof;++bad.snapshot.value;TreeCheckBudget v({});Check(!VerifyNativeArguments(g,CallableKind::kFunction,bad,v));
  TreeCheckBudget arrow({});Check(std::get<NativeArgumentsError>(ProveNativeArguments(g,CallableKind::kArrow,arrow))==NativeArgumentsError::kOwner);
  if(count){auto wrong=g;wrong.uses[0].operand=analysis::IntegerLiteral{1};Check(!Proves(wrong));wrong=g;std::get<ParameterEntry>(std::get<EntryDefinition>(wrong.values[0].origin)).index=count;Check(!Proves(wrong));}
  auto late=g;late.ops.insert(late.ops.begin(),ValueOp{ValueGlobalRead{},WorkBlockId{0},0,UseId{0},0,{},LanguageEffect::kGlobalAccess,LanguageEffect::kGlobalAccess,ExceptionalCompletion::kMayThrow,OriginId{0}});++late.blocks[0].op_count;Check(!Proves(late));
  if(count){auto projected=g;projected.input.values.clear();Check(!Proves(projected));}
  auto twice=g;twice.ops.push_back(g.ops[0]);++twice.blocks[0].op_count;Check(!Proves(twice));
  // Exact work and storage frontiers, not a guessed minimum grant.
  for(bool bytes:{false,true}){std::uint64_t lo=0,hi=10000000;while(lo<hi){auto mid=(lo+hi)/2;SemanticBudget grant{};if(bytes)grant.bytes=mid;else grant.work=mid;bool ok=false;try{ok=Proves(g,grant);}catch(TreeStructureError){}if(ok)hi=mid;else lo=mid+1;}SemanticBudget exact{};if(bytes)exact.bytes=lo;else exact.work=lo;Check(Proves(g,exact));if(lo){if(bytes)--exact.bytes;else --exact.work;bool refused=false;try{refused=!Proves(g,exact);}catch(TreeStructureError){refused=true;}Check(refused);}}
 }
 std::cout<<"native arguments: mutations, 200/201/400/800 and exact budgets passed\n";
}
