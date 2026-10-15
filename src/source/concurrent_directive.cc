#include "abcd/source/concurrent_directive.h"
#include "concurrent_completion.h"
#include "concurrent_async.h"
#include "function_context.h"
#include <new>
#include <stdexcept>
namespace abcd::source {
namespace {
// Same allocator contract as creator-class preparation: allocate_shared charges
// its actual rebound control block before allocation. The borrowed admission
// is used only during construction; destruction never dereferences it.
template<class T> struct DirectiveAllocator {
  using value_type = T;
  std::size_t* bytes;
  explicit DirectiveAllocator(std::size_t& left) : bytes(&left) {}
  template<class U> DirectiveAllocator(const DirectiveAllocator<U>& other) : bytes(other.bytes) {}
  T* allocate(std::size_t count) {
    if (count > *bytes / sizeof(T)) throw ConcurrentFailure{ConcurrentError::kResourceLimit};
    *bytes -= count * sizeof(T); return std::allocator<T>{}.allocate(count);
  }
  void deallocate(T* pointer, std::size_t count) noexcept { std::allocator<T>{}.deallocate(pointer, count); }
  template<class U> bool operator==(const DirectiveAllocator<U>&) const noexcept { return true; }
};
}  // namespace
std::size_t ConcurrentDirective::AllocatedBytes() const {
  return (requests_.capacity()+bindings_.capacity())*sizeof(abc::ModuleRequestId) +
      (async_ ? async_->allocated_bytes : 0);
}
std::variant<ConcurrentDirective,ConcurrentFailure> ProveConcurrentDirective(
    const FunctionContext& owner,const analysis::ValueProgram& graph,analysis::SemanticBudget budget,const abc::MethodBody* original) {
  try {
  if(!owner.Concurrent()||(owner.Callable()!=CallableKind::kFunction&&owner.Callable()!=CallableKind::kAsyncFunction)||
      ConcurrentOutput(owner.Identity().contract)!=ConcurrentOutputPolicy::kArkDirectiveV1) {
    ConcurrentFailure failure;failure.kind=ConcurrentError::kNotificationOwner;failure.method=owner.Target();return failure;
  }
  const auto& metadata=*owner.Concurrent();
  ConcurrentDirective result{owner.Target(),graph.revision,graph.cfg_revision};result.record_=metadata.location.record;result.identity_=owner.Identity();
  std::uint64_t completion_work = 0;
  if (owner.Callable() == CallableKind::kAsyncFunction) {
    if (!original || !metadata.asynchronous) { auto e=metadata.location; e.kind=ConcurrentError::kAsync; return e; }
    const auto before = budget.bytes;
    auto evidence = std::allocate_shared<ConcurrentAsyncEvidence>(DirectiveAllocator<ConcurrentAsyncEvidence>{budget.bytes});
    evidence->frame=owner.Frame(); evidence->profile=owner.Identity().profile;
    evidence->allocated_bytes=before-budget.bytes;
    const auto copy = [&]<class T>(std::vector<T>& into, const std::vector<T>& from) {
      if(from.size()>budget.bytes/sizeof(T))throw ConcurrentFailure{ConcurrentError::kResourceLimit};
      budget.bytes-=from.size()*sizeof(T); into=from;
      if(into.capacity()-from.size()>budget.bytes/sizeof(T))throw ConcurrentFailure{ConcurrentError::kResourceLimit};
      budget.bytes-=(into.capacity()-from.size())*sizeof(T); evidence->allocated_bytes+=into.capacity()*sizeof(T);
    };
    copy(evidence->original.instructions,original->instructions); copy(evidence->original.tries,original->tries); copy(evidence->original.handlers,original->handlers);
    auto checked=VerifyConcurrentAsync(*evidence,graph,budget);
    if(const auto* error=std::get_if<ConcurrentFailure>(&checked)){auto e=*error;e.method=owner.Target();e.record=metadata.location.record;return e;}
    completion_work=std::get<std::uint64_t>(checked);result.async_=std::move(evidence);
  } else {
    const auto checked=BuildConcurrentCompletion(graph,budget);
    if(const auto* error=std::get_if<ConcurrentFailure>(&checked)){auto failure=*error;failure.method=owner.Target();failure.record=metadata.location.record;return failure;}
    const auto& completion=std::get<ConcurrentCompletion>(checked);
    if(completion.allocated_bytes>budget.bytes) {ConcurrentFailure e;e.kind=ConcurrentError::kResourceLimit;return e;}
    budget.bytes-=completion.allocated_bytes;completion_work=completion.work;
  }
  std::size_t count=0;
  for(const auto n:{metadata.requests.entries.size(),metadata.module.imports.size(),metadata.module.namespace_imports.size()}) {
    if(n>budget.bytes/sizeof(abc::ModuleRequestId)-count) {auto e=metadata.location;e.kind=ConcurrentError::kResourceLimit;return e;}
    count+=n;
  }
  if(completion_work>budget.work||count>budget.work-completion_work) {auto e=metadata.location;e.kind=ConcurrentError::kWorkLimit;return e;}
  result.work_=completion_work+count;
  if(count>budget.bytes/sizeof(abc::ModuleRequestId)) {auto e=metadata.location;e.kind=ConcurrentError::kResourceLimit;return e;}
  result.requests_.reserve(metadata.requests.entries.size());
  result.bindings_.reserve(metadata.module.imports.size()+metadata.module.namespace_imports.size());
  if((result.requests_.capacity()+result.bindings_.capacity())*sizeof(abc::ModuleRequestId)>budget.bytes){auto e=metadata.location;e.kind=ConcurrentError::kResourceLimit;return e;}
  for(const auto& request:metadata.requests.entries)result.requests_.push_back(request.request);
  for(const auto& binding:metadata.module.imports)result.bindings_.push_back(binding.request);
  for(const auto& binding:metadata.module.namespace_imports)result.bindings_.push_back(binding.request);
  return result;
  }catch(const ConcurrentFailure& e){return e;}
  catch(const std::bad_alloc&){ConcurrentFailure e;e.kind=ConcurrentError::kResourceLimit;return e;}
  catch(const std::length_error&){ConcurrentFailure e;e.kind=ConcurrentError::kResourceLimit;return e;}
}
}
