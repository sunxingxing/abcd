#include "../../../src/source/cell_parameter_default.h"
#include "../../../src/source/cell_parameter_default_json.h"
#include "../../../src/source/family_route_failure.h"
#include "../../../tools/record_failure_json.h"
#include "../../../src/analysis/value_flow.h"
#include <algorithm>
#include <iostream>
#include <source_location>
#include <cstdlib>
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
namespace {
void Check(bool ok,std::source_location at=std::source_location::current()) {if(!ok){std::cerr<<"cell_parameter_default_test:"<<at.line()<<'\n';std::abort();}}
void Relink(ValueProgram& g) {
  for(auto& v:g.values)v.first_use.reset();std::vector<std::optional<UseId>> last(g.values.size());
  for(std::uint32_t i=0;i<g.uses.size();++i){auto& u=g.uses[i];u.previous.reset();u.next.reset();
    if(u.life!=ValueLife::kLive)continue;if(const auto* v=std::get_if<ValueId>(&u.operand)) {
      u.previous=last[v->value];if(u.previous)g.uses[u.previous->value].next=UseId{i};else g.values[v->value].first_use=UseId{i};last[v->value]=UseId{i};}}
}
struct Builder {
  ValueProgram g;
  WorkBlockId block;
  void Block(){block={static_cast<std::uint32_t>(g.blocks.size())};g.blocks.push_back({{static_cast<std::uint32_t>(g.ops.size())},0,{static_cast<std::uint32_t>(g.edges.size())},0});}
  void Edge(WorkBlockId target,FlowEdgeKind kind){auto& b=g.blocks[block.value];g.edges.push_back({block,target,kind,b.edge_count++});}
  OpId Op(ValueOperation value,std::initializer_list<std::pair<OperandRole,ValueOperand>> inputs={},bool result=false,
      LanguageEffect effects=LanguageEffect::kNone,bool throws=false) {
    const OpId id{static_cast<std::uint32_t>(g.ops.size())};const UseId first{static_cast<std::uint32_t>(g.uses.size())};
    for(auto [role,v]:inputs)g.uses.push_back({id,role,ReadPurpose::kOperand,v});
    std::optional<ValueId> out;
    if(result){out=ValueId{static_cast<std::uint32_t>(g.values.size())};g.values.push_back({OpResult{id},ValueKinds::Language()});}
    g.ops.push_back({value,block,g.blocks[block.value].op_count++,first,static_cast<std::uint32_t>(inputs.size()),out,
      effects,throws?effects:LanguageEffect::kNone,throws?ExceptionalCompletion::kMayThrow:ExceptionalCompletion::kImpossible,{id.value}});
    g.origins.push_back({{id.value},{id.value},abc::CodeOffset{id.value+100}});
    if(throws)g.exceptional_exits.push_back({id,{id.value},effects,{}});
    return id;
  }
  ValueId Result(OpId id){return *g.ops[id.value].result;}
  ValueOperand Copy(ValueOperand v,bool copies){return copies?ValueOperand{Result(Op(ValueCopy{},{{OperandRole::kInput,v}},true))}:v;}
};
enum class Adverse { kNone, kDuplicateGuard, kDoubleRead, kProvidedRead, kPrefixObservation, kMissingGuard, kBypass };
ValueProgram Chain(std::uint32_t count,bool copies=false,bool split=false,Adverse adverse=Adverse::kNone) {
  Builder b;for(std::uint32_t i=0;i<count;++i)b.g.values.push_back({EntryDefinition{ParameterEntry{i}},ValueKinds::Language()});
  b.Block();b.Op(ValueEnvironmentPush{static_cast<std::uint16_t>(count),{}},{},false,LanguageEffect::kEnvironmentWrite);
  std::optional<std::size_t> bypass;
  if(adverse==Adverse::kBypass){
    b.Op(ValueBranch{UnaryOperator::kTrueOrNonzeroNumber},{{OperandRole::kCondition,SpecialValue::kTrue}});
    b.Edge({b.block.value+1},FlowEdgeKind::kTaken);bypass=b.g.edges.size();b.Edge({0},FlowEdgeKind::kFallthrough);b.Block();
  }
  if(adverse==Adverse::kPrefixObservation)b.Op(ValueDebugger{},{},false,LanguageEffect::kGuard);
  for(std::uint32_t i=0;i<count;++i){
    if(adverse==Adverse::kProvidedRead)b.Op(ValueLexicalLoad{{{1},{0}}},{},true,LanguageEffect::kLexicalRead|LanguageEffect::kEnvironmentRead);
    const LexicalAddress local{{0},{static_cast<std::uint16_t>(i)}};
    b.Op(ValueLexicalStore{local},{{OperandRole::kStoredValue,b.Copy(ValueId{i},copies)}},false,LanguageEffect::kLexicalWrite|LanguageEffect::kEnvironmentRead);
    auto test=b.Result(b.Op(ValueLexicalLoad{local},{},true,LanguageEffect::kLexicalRead|LanguageEffect::kEnvironmentRead));
    const auto compared=b.Copy(test,copies);
    if(split){b.Edge({b.block.value+1},FlowEdgeKind::kFallthrough);b.Block();}
    auto comparison=b.Result(b.Op(ValueBinary{BinaryOperator::kStrictEqual},{{OperandRole::kLeft,compared},{OperandRole::kRight,SpecialValue::kUndefined}},true));
    b.Op(ValueBranch{UnaryOperator::kTrueOrNonzeroNumber},{{OperandRole::kCondition,b.Copy(comparison,copies)}});
    const auto header=b.block.value;const auto join=header+(split?4U:2U);
    b.Edge({header+1},FlowEdgeKind::kTaken);b.Edge({join},FlowEdgeKind::kFallthrough);b.Block();
    auto read=b.Result(b.Op(ValueLexicalLoad{{{1},{0}}},{},true,LanguageEffect::kLexicalRead|LanguageEffect::kEnvironmentRead));
    if(adverse==Adverse::kDoubleRead)b.Op(ValueLexicalLoad{{{1},{0}}},{},true,LanguageEffect::kLexicalRead|LanguageEffect::kEnvironmentRead);
    const auto rhs=b.Copy(read,copies);
    if(split){b.Edge({b.block.value+1},FlowEdgeKind::kFallthrough);b.Block();}
    if(adverse!=Adverse::kMissingGuard)b.Op(ValueGuard{abc::PoolIndex{17}},{{OperandRole::kGuardValue,rhs}},false,LanguageEffect::kGuard,true);
    if(adverse==Adverse::kMissingGuard)b.Op(ValueNop{});
    if(split){b.Edge({b.block.value+1},FlowEdgeKind::kFallthrough);b.Block();}
    if(adverse==Adverse::kDuplicateGuard)b.Op(ValueGuard{abc::PoolIndex{17}},{{OperandRole::kGuardValue,rhs}},false,LanguageEffect::kGuard,true);
    b.Op(ValueLexicalStore{local},{{OperandRole::kStoredValue,rhs}},false,LanguageEffect::kLexicalWrite|LanguageEffect::kEnvironmentRead);
    b.Edge({join},FlowEdgeKind::kFallthrough);b.Block();
  }
  if(bypass)b.g.edges[*bypass].target=b.block;
  for(std::uint32_t i=0;i<count;++i)b.Op(ValueLexicalLoad{{{0},{static_cast<std::uint16_t>(i)}}},{},true,LanguageEffect::kLexicalRead|LanguageEffect::kEnvironmentRead);
  b.Op(ValueReturn{},{{OperandRole::kReturnValue,SpecialValue::kUndefined}});Relink(b.g);return b.g;
}
struct Input {
  ValueEnvironments environments;CellPlan cells;ParameterPlan parameters;
  template<class T,class E> static T Take(std::variant<T,E> r) {
    if(const auto* e=std::get_if<E>(&r)){std::cerr<<"input error "<<static_cast<unsigned>(*e)<<'\n';std::abort();}
    return std::get<T>(std::move(r));
  }
  Input(const ValueProgram& g,std::uint32_t n)
      : environments(Take(BuildValueEnvironments(g))),
        cells(Take(BuildCellPlan(g,environments))),
        parameters(Take(BuildParameterPlan(g,n))) {}

};
CellParameterDefaultProof Build(const ValueProgram& g,const Input& in) {
  auto r=CellParameterDefaultProof::Build(g,in.environments,in.cells,in.parameters,0);
  if(const auto* f=std::get_if<CellParameterDefaultFailure>(&r))std::cerr<<"proof refusal "<<static_cast<unsigned>(f->kind)<<" op "<<(f->location.operation?f->location.operation->value:99999)<<'\n';
  Check(std::holds_alternative<CellParameterDefaultProof>(r));return std::get<CellParameterDefaultProof>(std::move(r));
}
void Scale() {
  for(auto n:{1U,2U,200U,201U,400U,800U}) {
    auto g=Chain(n,true,true);Input in(g,n);auto proof=Build(g,in);Check(proof.Defaults().size()==n);
    auto v=VerifyCellParameterDefaults(g,in.environments,in.cells,in.parameters,0,proof.Defaults());Check(std::holds_alternative<CellParameterDefaultCost>(v));
    const auto cost=proof.Cost();Check(cost.allocated_bytes<static_cast<std::size_t>(n+1)*60000);
    auto exact=CellParameterDefaultProof::Build(g,in.environments,in.cells,in.parameters,0,{cost.allocated_bytes,cost.work});Check(std::holds_alternative<CellParameterDefaultProof>(exact));
    auto short_bytes=CellParameterDefaultProof::Build(g,in.environments,in.cells,in.parameters,0,{cost.allocated_bytes-1,cost.work});
    Check(std::get<CellParameterDefaultFailure>(short_bytes).kind==CellParameterDefaultError::kResourceLimit);
    auto short_work=CellParameterDefaultProof::Build(g,in.environments,in.cells,in.parameters,0,{cost.allocated_bytes,cost.work-1});
    Check(std::get<CellParameterDefaultFailure>(short_work).kind==CellParameterDefaultError::kWorkLimit);
    const auto vc=std::get<CellParameterDefaultCost>(v);
    Check(std::holds_alternative<CellParameterDefaultCost>(VerifyCellParameterDefaults(g,in.environments,in.cells,in.parameters,0,proof.Defaults(),{vc.allocated_bytes,vc.work})));
    Check(std::holds_alternative<CellParameterDefaultFailure>(VerifyCellParameterDefaults(g,in.environments,in.cells,in.parameters,0,proof.Defaults(),{vc.allocated_bytes-1,vc.work})));
  }
}
void Variants() {
  for(bool copy:{false,true})for(bool split:{false,true}) {
    auto g=Chain(2,copy,split);Input in(g,2);auto proof=Build(g,in);Check(proof.Defaults().size()==2);
    // Change the SSA numbering without changing any semantic entry identity.
    std::swap(g.values[0],g.values[1]);
    for(auto& use:g.uses)if(auto* v=std::get_if<ValueId>(&use.operand);v&&v->value<2)v->value=1-v->value;
    Relink(g);Input reordered(g,2);Check(Build(g,reordered).Defaults().size()==2);
  }
  for(auto mode:{Adverse::kDuplicateGuard,Adverse::kDoubleRead,Adverse::kProvidedRead,Adverse::kPrefixObservation,Adverse::kMissingGuard,Adverse::kBypass}) {
    auto g=Chain(1,false,true,mode);Input in(g,1);
    Check(std::holds_alternative<CellParameterDefaultFailure>(CellParameterDefaultProof::Build(g,in.environments,in.cells,in.parameters,0)));
  }
}
void Mutations() {
  auto g=Chain(2);Input in(g,2);auto proof=Build(g,in);std::vector<CellParameterDefault> original(proof.Defaults().begin(),proof.Defaults().end());
  const auto reject=[&](const auto& records){Check(std::holds_alternative<CellParameterDefaultFailure>(VerifyCellParameterDefaults(g,in.environments,in.cells,in.parameters,0,records)));};
  auto d=original;std::swap(d[0].local,d[0].outer);reject(d);
  d=original;d[0].parameter=1;reject(d);
  d=original;std::swap(d[0],d[1]);reject(d);
  d=original;d[0].guard=d[0].read;reject(d);
  d=original;d[0].fallback_store=d[1].fallback_store;reject(d);
  d=original;std::swap(d[0].missing,d[0].provided);reject(d);
  d=original;d[0].destination.allocation=d[0].initial_store;reject(d);
  d=original;d[0].join={0};reject(d);
  auto changed=g;changed.exceptional_exits.erase(changed.exceptional_exits.begin());
  Check(std::holds_alternative<CellParameterDefaultFailure>(VerifyCellParameterDefaults(changed,in.environments,in.cells,in.parameters,0,original)));
  changed=g;changed.exceptional_exits.front().handler=ValueHandler{{},{original.back().join}};
  Check(std::holds_alternative<CellParameterDefaultFailure>(VerifyCellParameterDefaults(changed,in.environments,in.cells,in.parameters,0,original)));
  changed=g;changed.ops[original[0].guard.value].operation=ValueNop{};
  Check(std::holds_alternative<CellParameterDefaultFailure>(VerifyCellParameterDefaults(changed,in.environments,in.cells,in.parameters,0,original)));
  // Rebuild storage after changing actual input, not just the proposed certificate.
  changed=g;std::get<ValueLexicalStore>(changed.ops[original[0].fallback_store.value].operation).address.slot.value=1;
  Input wrong(changed,2);Check(std::holds_alternative<CellParameterDefaultFailure>(CellParameterDefaultProof::Build(changed,wrong.environments,wrong.cells,wrong.parameters,0)));
  changed=g;changed.ops[original[0].comparison.value].normal=LanguageEffect::kGuard;
  Check(std::holds_alternative<CellParameterDefaultFailure>(VerifyCellParameterDefaults(changed,in.environments,in.cells,in.parameters,0,original)));
}
void Json() {
  auto g=Chain(1);Input in(g,1);
  const auto diagnosis=DiagnoseCellParameterDefaults(g,in.environments,in.cells,in.parameters,0,{777},{});
  Check(diagnosis.verified);
  Check(diagnosis.guard.pc && diagnosis.guard.working_pc && diagnosis.guard.pc->value==diagnosis.guard.working_pc->value+100);
  static_assert(std::is_trivially_copyable_v<CellParameterDefaultFailure>);
  static_assert(std::is_trivially_copyable_v<CellParameterDefaultDiagnostic>);
  const auto encoded=CellParameterDefaultJson(diagnosis);
  const auto decoded=ParseCellParameterDefault(nlohmann::json::parse(encoded.dump()));
  Check(decoded&&CellParameterDefaultJson(*decoded)==encoded);
  auto malformed=encoded;malformed["guard"]["pc"]=-1;Check(!ParseCellParameterDefault(malformed));
  malformed=encoded;malformed["outer"]["slot"]=UINT32_MAX;Check(!ParseCellParameterDefault(malformed));

  for(unsigned reason=0;reason<=static_cast<unsigned>(CellParameterDefaultError::kWorkLimit);++reason) {
    CellParameterDefaultDiagnostic d;d.evidence.kind=static_cast<CellParameterDefaultError>(reason);d.evidence.parameter=0;
    d.evidence.location.pc=abc::CodeOffset{0};d.evidence.location.working_pc=abc::CodeOffset{12};d.evidence.location.method=abc::EntityOffset{44};
    FamilyTreeFailure f{{0},FunctionAbiError::kLength};f.cell_parameter_default=d;
    const auto route=FamilyRouteFailure(FamilyPrepareFailure{f});Check(route.cell_parameter_default.has_value());Check(route.failure_class==RouteFailureClass::kUnproved);
    MethodRouteReceipt receipt;
    receipt.identity.file_size=4096;receipt.identity.file_hash.bytes[0]=1;
    receipt.identity.abc_version={{12,0,6,0}};receipt.identity.profile=abc::ProfileId::kDynamic1206;
    receipt.method=abc::EntityOffset{777};receipt.record=abc::EntityOffset{111};receipt.route=SourceRoute::kValueTree;
    receipt.stage=SourceStage::kValuesVerified;receipt.work_revision=0;receipt.coverage=Coverage::kUnsupported;receipt.failure=route;
    const auto serialized=SerializeRouteReceipt(receipt);Check(std::holds_alternative<std::string>(serialized));
    const auto read=ParseRouteReceipt(std::get<std::string>(serialized));Check(std::holds_alternative<MethodRouteReceipt>(read));
    Check(std::get<MethodRouteReceipt>(read).failure->cell_parameter_default.has_value());
    Check(CellParameterDefaultJson(*std::get<MethodRouteReceipt>(read).failure->cell_parameter_default)==CellParameterDefaultJson(d));
    Check(route.causes.entries[0].domain==FailureDomain<FunctionAbiError>());
    auto json=CellParameterDefaultJson(*route.cell_parameter_default);auto parsed=ParseCellParameterDefault(nlohmann::json::parse(json.dump()));Check(parsed.has_value());Check(CellParameterDefaultJson(*parsed)==json);
    nlohmann::json a,b;tools::DescribeRecordFailure(RouteRefusal{route},a);RecordProgramFailure rf{RecordProgramError::kFunction,{}};rf.cell_parameter_default=d;tools::DescribeRecordFailure(rf,b);
    Check(a["source_failure"]["cell_parameter_default"]==b["source_cell_parameter_default"]);
    for(const auto kind:{FamilyTreeError::kResourceLimit,FamilyTreeError::kWorkLimit}) {
      f.cause=kind;const auto classified=FamilyRouteFailure(FamilyPrepareFailure{f});
      Check(classified.failure_class==(kind==FamilyTreeError::kResourceLimit?RouteFailureClass::kMemoryLimit:RouteFailureClass::kWorkLimit));
    }
    d.evidence.location.pc.reset();d.evidence.location.working_pc.reset();Check(!CellParameterDefaultJson(d)["location"].contains("pc"));
  }
}
}
int main(){static_assert(!std::is_default_constructible_v<CellParameterDefaultProof>);Scale();Variants();Mutations();Json();}
