#ifndef ABCD_SOURCE_CONCURRENT_DIRECTIVE_H_
#define ABCD_SOURCE_CONCURRENT_DIRECTIVE_H_
#include "abcd/source/concurrent_failure.h"
#include <memory>
#include "abcd/source/source_route.h"
namespace abcd::source {
class FunctionContext;
struct ConcurrentCompletion;
struct ConcurrentAsyncEvidence;
class ConcurrentDirective {
 public:
  const RouteInputIdentity& Identity() const { return identity_; }
  abc::EntityOffset Method() const { return method_; }
  analysis::IrRevision Revision() const { return revision_; }
  analysis::CfgRevision CfgRevision() const { return cfg_revision_; }
  abc::EntityOffset Record() const { return record_; }
  std::span<const abc::ModuleRequestId> Requests() const { return requests_; }
  std::span<const abc::ModuleRequestId> Bindings() const { return bindings_; }
  std::size_t AllocatedBytes() const;
  const ConcurrentAsyncEvidence* Async() const { return async_.get(); }
  std::uint64_t Work() const { return work_; }
  bool operator==(const ConcurrentDirective&) const = default;
 private:
  ConcurrentDirective(abc::EntityOffset m,analysis::IrRevision r,analysis::CfgRevision c):method_(m),revision_(r),cfg_revision_(c){}
  std::shared_ptr<const ConcurrentAsyncEvidence> async_;
  RouteInputIdentity identity_;
  std::uint64_t work_=0;
  abc::EntityOffset record_;std::vector<abc::ModuleRequestId> requests_,bindings_;
  abc::EntityOffset method_;analysis::IrRevision revision_;analysis::CfgRevision cfg_revision_;
  friend std::variant<ConcurrentDirective,ConcurrentFailure> ProveConcurrentDirective(
      const FunctionContext&,const analysis::ValueProgram&,analysis::SemanticBudget,const abc::MethodBody*);
};
[[nodiscard]] std::variant<ConcurrentDirective,ConcurrentFailure> ProveConcurrentDirective(
    const FunctionContext&,const analysis::ValueProgram&,analysis::SemanticBudget = {},const abc::MethodBody* = nullptr);
}
#endif
