#include "../../../src/source/capture_tree.h"
#include "../../../src/source/function_values.h"
#include "../../../src/source/function_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/source/recovery_flow.h"
#include "../../../src/source/source_variables.h"
#include "../../../src/source/class_tree.h"
#include "analysis/ssa_fixture.h"
#include <iostream>
#include <source_location>
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
namespace {
void Check(bool ok, std::source_location at = std::source_location::current()) {
  if (!ok) { std::cerr << "class deferred: " << at.line() << '\n'; std::abort(); }
}
template<class T, class... U> T Take(std::variant<U...> value,
    std::source_location at = std::source_location::current()) {
  if (auto* result=std::get_if<T>(&value)) return std::move(*result);
  std::cerr << "unexpected variant at " << at.line() << " index " << value.index() << '\n'; std::abort();
}
void Run(unsigned count, bool split, bool copy = false) {
  std::vector<std::uint8_t> code;
  const auto opcode = [&](abc::Opcode op) {
    const auto value = static_cast<unsigned>(op);
    code.push_back(value & UINT8_MAX); if (value > UINT8_MAX) code.push_back(value >> 8);
  };
  const auto word = [&](unsigned value) { code.push_back(value & UINT8_MAX); code.push_back(value >> 8); };
  opcode(abc::Opcode::kWIDE_NEWLEXENV_PREF_IMM16); word(count);
  if (split) { opcode(abc::Opcode::kJMP_IMM8); code.push_back(2); }
  for (unsigned i = 0; i < count; ++i) {
    if (split) { opcode(abc::Opcode::kJMP_IMM8); code.push_back(2); }
    opcode(abc::Opcode::kLDA_V8); code.push_back(copy ? 1 : 0);
    if (copy) {
      opcode(abc::Opcode::kSTA_V8); code.push_back(0);
      opcode(abc::Opcode::kLDA_V8); code.push_back(0);
    }
    opcode(abc::Opcode::kWIDE_STLEXVAR_PREF_IMM16_IMM16); word(0); word(i);
  }
  opcode(abc::Opcode::kRETURNUNDEFINED);
  const auto body = test::Body(code);
  const auto frame = Take<abc::FrameLayout>(abc::BuildFrameLayout(copy ? 1 : 0, 1, {false,false,false,false}));
  RouteInputIdentity identity; identity.profile=abc::ProfileId::kDynamic1206; identity.abc_version={12,0,6,0};
  const auto input = Take<FunctionInput>(FunctionInput::Build(identity,{300},{100},body,frame,InputCaptureMode::kClosed));
  auto values = Take<FunctionValues>(BuildFunctionValues(input,body,frame));
  auto& graph=values.graph;
  auto control=Take<ControlPlan>(BuildControlPlan(graph));
  auto evaluation=Take<EvaluationPlan>(BuildEvaluationPlan(graph));
  Check(evaluation.cells.has_value());
  std::vector<DeferredCellInitialization> deferred;
  for (std::size_t i=0;i<graph.ops.size();++i)
    if (std::holds_alternative<ValueLexicalStore>(graph.ops[i].operation))
      deferred.push_back({*evaluation.cells->cells.operations[i],OpId{static_cast<std::uint32_t>(i)}});
  Check(deferred.size()==count);
  const auto bytes=evaluation.cells->cells.cells.size()*sizeof(std::optional<OpId>);
  auto low=evaluation;
  Check(!MarkDeferredCells(graph,*low.cells,deferred,{bytes-1}));
  // Cross-block deferral now independently rebuilds working-flow dominance;
  // its simultaneous scratch must be admitted as well as the retained table.
  const auto mark=[&](std::size_t allowance,std::uint64_t work=SemanticBudget{}.work) {
    auto copy=evaluation;return MarkDeferredCells(graph,*copy.cells,deferred,{allowance,work});
  };
  std::size_t lower=bytes,upper=SemanticBudget{}.bytes;
  Check(mark(upper).has_value());
  while(lower<upper){const auto mid=lower+(upper-lower)/2;if(mark(mid))upper=mid;else lower=mid+1;}
  Check(lower && !mark(lower-1));
  const auto work=*mark(lower);Check(mark(lower,work).has_value());Check(work && !mark(lower,work-1));
  Check(MarkDeferredCells(graph,*evaluation.cells,deferred,{lower,work}).has_value());
  const auto verify=[&](std::size_t allowance,std::uint64_t steps=SemanticBudget{}.work) {
    return VerifyDeferredCells(graph,*evaluation.cells,{allowance,steps});
  };
  std::size_t first=0,last=SemanticBudget{}.bytes;
  Check(verify(last).has_value());
  while(first<last){const auto mid=first+(last-first)/2;if(verify(mid))last=mid;else first=mid+1;}
  if(first)Check(!verify(first-1));
  const auto verify_work=*verify(first);Check(verify(first,verify_work).has_value());
  Check(verify_work && !verify(first,verify_work-1));
  std::cout<<"deferred cells="<<count<<" split="<<split<<" copy="<<copy
    <<" mark="<<lower<<'/'<<work<<" verify="<<first<<'/'<<verify_work<<'\n';
  auto invalid = *evaluation.cells;
  invalid.deferred[deferred.front().cell.value] = OpId{0};
  Check(!VerifyDeferredCells(graph,invalid,{}));
  FunctionSyntax syntax;syntax.identity=identity;syntax.method={300};syntax.owner={1};
  syntax.callable=CallableKind::kFunction;syntax.strictness=Strictness::kStrict;syntax.parameters.resize(1);
  std::vector<ExpressionConstant> constants(graph.ops.size());
  const ExpressionConstants expressions{constants,0,0};
  auto built=BuildFunctionTree(graph,control,evaluation,syntax,expressions);
  Check(std::holds_alternative<ConstructedFunction>(built));
  auto tree=Take<ConstructedFunction>(std::move(built)).tree;
  auto checked=VerifyTreeSemantics(graph,control,evaluation,tree,expressions);
  Check(std::holds_alternative<TreeSemantics>(checked));
  auto flow=Take<TreeSemantics>(std::move(checked)).flow;
  CaptureCells request{};request.identity=identity;request.parent={300};request.child={400};
  request.parent_revision=graph.revision;request.parent_cfg=graph.cfg_revision;request.deferred=deferred;
  const auto valid=[&](const CaptureCells& r,const FunctionTree& t,const TreeFlow& f,SemanticBudget budget=SemanticBudget{}) {
    return VerifyDeferredCaptureTree(r,{t,f},budget);
  };
  Check(Take<std::uint64_t>(valid(request,tree,flow))==count);
  Check(Take<std::uint64_t>(valid(request,tree,flow,{0,count}))==count);
  Check(Take<CaptureTreeError>(valid(request,tree,flow,{0,count-1}))==CaptureTreeError::kWorkLimit);
  auto forged=request;forged.parent={301};Check(std::holds_alternative<CaptureTreeError>(valid(forged,tree,flow)));
  forged=request;++forged.parent_cfg.value;Check(std::holds_alternative<CaptureTreeError>(valid(forged,tree,flow)));
  forged=request;++forged.parent_revision.value;Check(std::holds_alternative<CaptureTreeError>(valid(forged,tree,flow)));
  forged=request;forged.deferred[0].store={0};Check(std::holds_alternative<CaptureTreeError>(valid(forged,tree,flow)));
  forged=request;forged.deferred[0].cell={count};Check(std::holds_alternative<CaptureTreeError>(valid(forged,tree,flow)));
  if (count>1) { forged=request;forged.deferred[0].store=deferred[1].store;Check(std::holds_alternative<CaptureTreeError>(valid(forged,tree,flow))); }
  const auto cell=deferred.front().cell.value;
  const auto declaration=*flow.original->cells->bindings[cell].declaration;
  const auto symbol=flow.original->cells->bindings[cell].binding;
  auto early=tree;std::get<DeclarationStmt>(early.statements[declaration.value].statement).initializer.reset();
  Check(std::holds_alternative<CaptureTreeError>(valid(request,early,flow)));
  Check(!std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph,control,evaluation,early,expressions)));
  auto moved=tree;++moved.symbols[symbol.value].scope.value;
  Check(std::holds_alternative<CaptureTreeError>(valid(request,moved,flow)));
  moved=tree;moved.symbols[symbol.value].declaration=DeclarationKind::kConst;
  Check(std::holds_alternative<CaptureTreeError>(valid(request,moved,flow)));
  auto wrong=flow;auto original=std::make_shared<OriginalFlow>(*flow.original);
  original->cells->bindings[cell].activation=OriginalCellActivation{{0},declaration};wrong.original=original;
  Check(std::holds_alternative<CaptureTreeError>(valid(request,tree,wrong)));
  // Several independent requests for one storage do not duplicate declarations.
  request.deferred.insert(request.deferred.end(),deferred.begin(),deferred.end());
  Check(Take<std::uint64_t>(valid(request,tree,flow))==2*count);
  const auto variables=Take<SourceVariables>(PrepareSourceVariables(tree,{},flow.original.get()));
  // An absent activation is not upgraded by an external request. The canonical
  // declaration remains anchored to its original store even after recovery.
  for (const auto& entry:deferred) {
    const auto& binding=flow.original->cells->bindings[entry.cell.value];
    Check(!binding.activation && binding.declaration==flow.statements[entry.store.value]);
  }
}
// The immutable working graph is well-formed; only the proposed storage owner
// is forged. All ids/revisions stay valid, so cross-block dominance must reject
// a future allocation as the owner of an earlier store.
void WorkingActivation() {
  const auto op=[](abc::Opcode value){return static_cast<std::uint8_t>(value);};
  const auto body=test::Body({op(abc::Opcode::kNEWLEXENV_IMM8),1,
      op(abc::Opcode::kLDA_V8),0,op(abc::Opcode::kSTLEXVAR_IMM4_IMM4),0,
      op(abc::Opcode::kJMP_IMM8),2,op(abc::Opcode::kNEWLEXENV_IMM8),1,
      op(abc::Opcode::kPOPLEXENV),op(abc::Opcode::kRETURNUNDEFINED)});
  const auto frame=Take<abc::FrameLayout>(abc::BuildFrameLayout(0,1,{false,false,false,false}));
  RouteInputIdentity identity;identity.profile=abc::ProfileId::kDynamic1206;identity.abc_version={12,0,6,0};
  const auto input=Take<FunctionInput>(FunctionInput::Build(identity,{300},{100},body,frame,InputCaptureMode::kClosed));
  const auto values=Take<FunctionValues>(BuildFunctionValues(input,body,frame));
  const auto& graph=values.graph;
  auto evaluation=Take<EvaluationPlan>(BuildEvaluationPlan(graph));
  std::vector<OpId> allocations;std::optional<DeferredCellInitialization> request;
  for(std::size_t i=0;i<graph.ops.size();++i){
    if(std::holds_alternative<ValueEnvironmentPush>(graph.ops[i].operation))allocations.push_back({static_cast<std::uint32_t>(i)});
    if(std::holds_alternative<ValueLexicalStore>(graph.ops[i].operation))request=DeferredCellInitialization{*evaluation.cells->cells.operations[i],{static_cast<std::uint32_t>(i)}};
  }
  Check(allocations.size()==2 && request.has_value());
  Check(graph.ops[allocations.back().value].block!=graph.ops[request->store.value].block);
  Check(MarkDeferredCells(graph,*evaluation.cells,{&*request,1},{}).has_value());
  auto forged=*evaluation.cells;
  std::get<AllocatedCell>(forged.cells.cells[request->cell.value].storage).allocation=allocations.back();
  Check(!VerifyDeferredCells(graph,forged,{}));
  Check(!MarkDeferredCells(graph,forged,{&*request,1},{}));
}
void FilePreparation(const char* path) {
  const auto file=Take<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(path));
  const auto hash=Take<corpus::FileFingerprint>(file->Fingerprint());
  const auto header=Take<abc::Header>(abc::ParseHeader(std::span(hash.prefix).first(hash.prefix_size),hash.size));
  const auto index=Take<abc::MetadataIndex>(abc::BuildMetadataIndex(*file,header));
  const auto lookup=Take<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto roles=Take<MethodRoleIndex>(MethodRoleIndex::Build(*file,header,index,lookup));
  const auto functions=Take<FunctionCreationSites>(FunctionCreationSites::Build(*file,header,index,lookup));
  const auto classes=Take<ClassMethodSites>(ClassMethodSites::Build(*file,header,index,lookup));
  auto identity=MakeRouteInputIdentity(hash,header);
  identity.contract=SourceContract::kLanguageNumericConstructorsV1;
  std::optional<abc::MethodPosition> entry;
  for(std::size_t i=0;i<index.methods.size();++i)
    if(index.methods[i].name=="func_main_0") entry=abc::MethodPosition{static_cast<std::uint32_t>(i)};
  Check(entry.has_value());
  const auto packaging=Take<abc::FilePackaging>(abc::DetectFilePackaging(*file,header,index));
  const auto record=std::find_if(index.records.begin(),index.records.end(),[&](const auto& record) {
    return record.offset==index.methods[entry->value].owner_offset;
  });
  Check(record!=index.records.end());
  const auto modules=Take<RecordModuleContext>(ReadRecordModuleContext(*file,header,index,*record,packaging));
  const auto root_family=Take<FileFamily>(ReadFunctionFamily(*file,header,index,lookup,roles,functions,
      *entry,identity,{},&modules,nullptr,{},FamilyClassMode::kRetainRoot));
  const auto& creator=root_family.members.front().function.Normal();
  Check(creator.values.classes.classes.size()==1);
  const auto operation=creator.values.classes.classes.front().operation;
  const auto prepare=[&](SemanticBudget budget) {
    return PrepareClassTree(*file,header,index,lookup,roles,functions,classes,creator,operation,
        identity,nullptr,budget,nullptr,{},FunctionOwnerId{37},RecoveryMode::kCompleteOnly);
  };
  const auto prepared=Take<ClassPreparation>(prepare({}));
  Check(prepared.next_owner>37 && prepared.bodies.size()==3);
  // Retained/preparation charge is not the transient peak of nested readers.
  // Find the actual byte boundary without weakening any production quota.
  std::size_t low=0,high=SemanticBudget{}.bytes;
  while(low<high) {
    const auto mid=low+(high-low)/2;
    if(std::holds_alternative<ClassPreparation>(prepare({mid}))) high=mid;
    else low=mid+1;
  }
  Check(low>0 && prepared.allocated_bytes<=low);
  Check(std::holds_alternative<ClassPreparation>(prepare({low,prepared.work})));
  Check(std::holds_alternative<ClassTreeFailure>(prepare({low-1,prepared.work})));
  Check(std::holds_alternative<ClassTreeFailure>(prepare({low,prepared.work-1})));
  unsigned requests=0;
  for(const auto& body:prepared.bodies) {
    const auto& residual=std::get<FamilyPreparation>(body);
    const auto* cells=residual.RootCapture();
    if(!cells || cells->deferred.empty()) continue;
    ++requests;
    const auto& owner=residual.Context();
    auto family=Take<FileFamily>(ReadFunctionFamily(*file,header,index,lookup,roles,functions,
        owner.Position(),identity,{},nullptr,&owner,{},FamilyClassMode::kReject,nullptr,{37},&creator));
    const auto root=family.members.front().function.Capture();
    Check(std::holds_alternative<std::uint64_t>(VerifyClassRootCaptureCells(*file,header,index,lookup,
        creator,root,owner,*cells)));
    auto forged=*cells;forged.deferred.clear();
    Check(std::holds_alternative<CaptureCellFailure>(VerifyClassRootCaptureCells(*file,header,index,lookup,
        creator,root,owner,forged)));
    forged=*cells;forged.targets.front().target=ParentCell{CellId{999}};
    Check(std::holds_alternative<CaptureCellFailure>(VerifyClassRootCaptureCells(*file,header,index,lookup,
        creator,root,owner,forged)));
  }
  Check(requests==2); // read and forwarded are distinct requests for one storage.
}
}
int main(int argc,char** argv) {
  for (auto size:{1U,200U,201U,400U,800U}) { Run(size,false);Run(size,true);Run(size,true,true); }
  WorkingActivation();
  Check(argc==2);FilePreparation(argv[1]);
}
