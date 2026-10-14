#include "../../../src/source/record_print_sink.h"
#include "../../../src/source/borrowed_print_trees.h"
#include "../../../src/source/tree_structure.h"
#include "abcd/source/tree_printer.h"
#include "abcd/source/counting_sink.h"
#include <cstdlib>
#include <new>
#include <sstream>
#include <source_location>
#include <iostream>
using namespace abcd;using namespace abcd::source;
namespace audit {
struct alignas(std::max_align_t) Header {void* base;std::size_t bytes,epoch;};
std::size_t epoch=0,live=0,peak=0;bool active=false,undercharged=false;PrintMemory* ledger=nullptr;
void* Allocate(std::size_t bytes,std::size_t alignment) {
  const auto extra=sizeof(Header)+alignment;
  if(bytes>SIZE_MAX-extra)throw std::bad_alloc();
  void* base=std::malloc(bytes+extra);if(!base)throw std::bad_alloc();
  auto address=(reinterpret_cast<std::uintptr_t>(base)+extra-1)&~(alignment-1);
  auto* h=reinterpret_cast<Header*>(address)-1;*h={base,bytes,active?epoch:0};
  if(active){live+=bytes;peak=std::max(peak,live);if(ledger&&live>ledger->Live())undercharged=true;}
  return reinterpret_cast<void*>(address);
}
void Free(void* p) {if(!p)return;auto* h=static_cast<Header*>(p)-1;if(active&&h->epoch==epoch)live-=h->bytes;std::free(h->base);}
void Begin(PrintMemory* current=nullptr){++epoch;live=peak=0;undercharged=false;ledger=current;active=true;}
void End(){active=false;ledger=nullptr;}
}
void* operator new(std::size_t n){return audit::Allocate(n,alignof(std::max_align_t));}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{audit::Free(p);}
void operator delete[](void* p)noexcept{audit::Free(p);}
void operator delete(void* p,std::size_t)noexcept{audit::Free(p);}
void operator delete[](void* p,std::size_t)noexcept{audit::Free(p);}
void* operator new(std::size_t n,std::align_val_t a){return audit::Allocate(n,static_cast<std::size_t>(a));}
void* operator new[](std::size_t n,std::align_val_t a){return ::operator new(n,a);}
void operator delete(void* p,std::align_val_t)noexcept{audit::Free(p);}
void operator delete[](void* p,std::align_val_t)noexcept{audit::Free(p);}
void operator delete(void* p,std::size_t,std::align_val_t)noexcept{audit::Free(p);}
void operator delete[](void* p,std::size_t,std::align_val_t)noexcept{audit::Free(p);}
namespace {
void Check(bool ok,std::source_location at=std::source_location::current()){if(!ok){std::cerr<<at.line()<<'\n';std::abort();}}
FunctionTree Tree(std::size_t n) {
 FunctionTree t{};t.method={123};t.owner={7};t.callable=CallableKind::kFunction;t.strictness=Strictness::kStrict;
 t.parameter_scope={0};t.body_scope={1};t.body={0};t.scopes={{ScopeKind::kParameters,{}},{ScopeKind::kFunctionBody,ScopeId{0}}};
 t.origins={EntryOrigin{}};t.statements.push_back({BlockStmt{{1},0,static_cast<std::uint32_t>(n)},{0}});
 for(std::size_t i=0;i<n;++i){t.children.push_back({static_cast<std::uint32_t>(i+1)});t.statements.push_back({DebuggerStmt{},{0}});}return t;
}
}
int main(){
 // A mutation that returns credit before destruction is observable immediately.
 {PrintMemory memory(32);auto lease=memory.Reserve(16);audit::Begin(&memory);
  auto data=std::make_unique<char[]>(16);lease.Allocated(16);Check(!lease.Release());
  Check(memory.Live()==16&&memory.Tracked()==16&&memory.Invariant()&&!audit::undercharged);
  data.reset();lease.Freed(16);Check(lease.Release()&&memory.Live()==0);Check(audit::live==0);audit::End();}
 // Tracked allocator independently detects omission of admission, not merely
 // agreement between two counters updated by the allocation implementation.
 {PrintMemory memory(0);audit::Begin(&memory);auto p=std::make_unique<char[]>(8);Check(audit::undercharged);p.reset();audit::End();}
 // Structural checking returns live expression scopes; its other arenas are
 // already destroyed. Neither their peak nor the input charge is that vector.
 {auto tree=Tree(1);tree.expressions.push_back({TreeLiteral{NullLiteral{}},{0}});
  tree.statements[1].statement=ExpressionStmt{ExprId{0}};
  const auto bound=std::get<TreeStructure>(VerifyTreeStructure(tree)).allocated_bytes;
  PrintMemory memory(bound);auto lease=memory.Reserve(bound);audit::Begin(&memory);
  auto checked=VerifyTreeStructure(tree,{bound});auto& result=std::get<TreeStructure>(checked);
  Check(result.retained_bytes==result.expression_scopes.capacity()*sizeof(ScopeId));
  Check(result.retained_bytes>0&&audit::live==result.retained_bytes&&!audit::undercharged);
  lease.Allocated(result.retained_bytes);Check(!lease.Release());
  const auto live=result.retained_bytes;
  std::vector<ScopeId>().swap(result.expression_scopes);lease.Freed(live);
  Check(audit::live==0&&lease.Release());audit::End();}
 for(std::size_t n:{200,201,400,800}) {
  {PrintMemory memory((2*n+1)*sizeof(std::uint32_t));audit::Begin(&memory);
   {PrintArray<std::uint32_t> array(memory);array.Resize(n);array.Resize(n+1);
    Check(memory.Tracked()==(n+1)*sizeof(std::uint32_t));Check(!audit::undercharged&&memory.Invariant());}
   Check(memory.Live()==0&&audit::live==0);audit::End();}
  {PrintMemory memory((2*n+1)*sizeof(std::uint32_t)-1);PrintArray<std::uint32_t> array(memory);array.Resize(n);
   try{array.Resize(n+1);Check(false);}catch(const TreePrintFailure& f){Check(f.kind==TreePrintError::kResourceLimit);}
   Check(array.Size()==n&&memory.Tracked()==n*sizeof(std::uint32_t));}
  auto tree=Tree(n);
  const auto measured=std::get<TreeStorage>(MeasureTreeStorage(tree));
  const auto owned=std::get<TreeStructure>(VerifyTreeStructure(tree));Check(owned.input_bytes==measured.bytes);
  PrintMemory owner(measured.bytes);auto held=owner.Reserve(measured.bytes);PrintMemory index(4096);
  {BorrowedPrintTrees scope(&tree,nullptr,held,index,{});
   const auto borrowed=std::get<TreeStructure>(VerifyTreeStructure(tree));Check(borrowed.input_bytes==0);
   Check(owned.allocated_bytes==borrowed.allocated_bytes+measured.bytes);
   auto foreign=Tree(1);Check(std::get<TreeStructure>(VerifyTreeStructure(foreign)).input_bytes!=0);
   Check(held.Release());Check(!BorrowedPrintTrees::Holds(tree));
   Check(std::get<TreeStructure>(VerifyTreeStructure(tree)).input_bytes==measured.bytes);}
  CountingSink counter;std::ostream counted(&counter);
  auto first=PrintFunctionTree(tree,counted);Check(std::holds_alternative<PrintedFunction>(first));
  const auto facts=std::get<PrintedFunction>(std::move(first));
  std::ostringstream expected;Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree,expected)));
  const auto bytes=counter.Bytes();Check(bytes==expected.str().size());
  PrintMemory memory(bytes+facts.allocated_bytes);
  audit::Begin(&memory);
  {
   RecordPrintSink sink(memory,bytes);std::ostream output(&sink);
   {
    auto scratch=memory.Reserve(memory.Remaining());
    auto printed=PrintFunctionTree(tree,output,{bytes},{scratch.Bytes(),facts.work});
    Check(std::holds_alternative<PrintedFunction>(printed));
    Check(!audit::undercharged&&memory.Invariant());
   }
   Check(memory.Live()==bytes&&audit::live==bytes);
   audit::End();auto text=sink.Finish();Check(text.View()==expected.str());
   try{auto extra=memory.Reserve(0);Check(false);}catch(const TreePrintFailure& f){Check(f.kind==TreePrintError::kUnsupportedContext);}
  }
  for(const auto short_work:{false,true}) {
   CountingSink sink;std::ostream output(&sink);
   auto denied=PrintFunctionTree(tree,output,{short_work?bytes:bytes-1},{facts.allocated_bytes,short_work?facts.work-1:facts.work});
   Check(std::get<TreePrintFailure>(denied).kind==(short_work?TreePrintError::kWorkLimit:TreePrintError::kOutputLimit));
  }
  {PrintMemory too_small(bytes-1);audit::Begin(&too_small);
   try{RecordPrintSink denied(too_small,bytes);Check(false);}catch(const TreePrintFailure& f){Check(f.kind==TreePrintError::kResourceLimit);}
   Check(audit::peak==0&&too_small.Live()==0);audit::End();}
  std::cout<<n<<" counted "<<bytes<<" scratch "<<facts.allocated_bytes<<" work "<<facts.work<<'\n';
 }
}
