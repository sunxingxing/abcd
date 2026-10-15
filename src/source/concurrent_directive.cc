#include "abcd/source/concurrent_directive.h"
#include "concurrent_completion.h"
#include "function_context.h"
#include <new>
#include <stdexcept>
namespace abcd::source {
std::variant<ConcurrentDirective,ConcurrentFailure> ProveConcurrentDirective(
    const FunctionContext& owner,const analysis::ValueProgram& graph,analysis::SemanticBudget budget) {
  try {
  if(!owner.Concurrent()||owner.Callable()!=CallableKind::kFunction||
      ConcurrentOutput(owner.Identity().contract)!=ConcurrentOutputPolicy::kArkDirectiveV1) {
    ConcurrentFailure failure;failure.kind=ConcurrentError::kNotificationOwner;failure.method=owner.Target();return failure;
  }
  const auto checked=BuildConcurrentCompletion(graph,budget);
  if(const auto* error=std::get_if<ConcurrentFailure>(&checked)){auto failure=*error;failure.method=owner.Target();failure.record=owner.Concurrent()->location.record;return failure;}
  const auto& completion=std::get<ConcurrentCompletion>(checked);
  if(completion.allocated_bytes>budget.bytes) {ConcurrentFailure e;e.kind=ConcurrentError::kResourceLimit;return e;}
  budget.bytes-=completion.allocated_bytes;
  const auto& metadata=*owner.Concurrent();
  ConcurrentDirective result{owner.Target(),graph.revision,graph.cfg_revision};result.record_=metadata.location.record;result.identity_=owner.Identity();
  std::size_t count=0;
  for(const auto n:{metadata.requests.entries.size(),metadata.module.imports.size(),metadata.module.namespace_imports.size()}) {
    if(n>budget.bytes/sizeof(abc::ModuleRequestId)-count) {auto e=metadata.location;e.kind=ConcurrentError::kResourceLimit;return e;}
    count+=n;
  }
  if(completion.work>budget.work||count>budget.work-completion.work) {auto e=metadata.location;e.kind=ConcurrentError::kWorkLimit;return e;}
  result.work_=completion.work+count;
  if(count>budget.bytes/sizeof(abc::ModuleRequestId)) {auto e=metadata.location;e.kind=ConcurrentError::kResourceLimit;return e;}
  result.requests_.reserve(metadata.requests.entries.size());
  result.bindings_.reserve(metadata.module.imports.size()+metadata.module.namespace_imports.size());
  if(result.AllocatedBytes()>budget.bytes){auto e=metadata.location;e.kind=ConcurrentError::kResourceLimit;return e;}
  for(const auto& request:metadata.requests.entries)result.requests_.push_back(request.request);
  for(const auto& binding:metadata.module.imports)result.bindings_.push_back(binding.request);
  for(const auto& binding:metadata.module.namespace_imports)result.bindings_.push_back(binding.request);
  return result;
  }catch(const std::bad_alloc&){ConcurrentFailure e;e.kind=ConcurrentError::kResourceLimit;return e;}
  catch(const std::length_error&){ConcurrentFailure e;e.kind=ConcurrentError::kResourceLimit;return e;}
}
}
