#ifndef ABCD_SOURCE_TREE_PRINT_TRACE_H_
#define ABCD_SOURCE_TREE_PRINT_TRACE_H_
#include "abcd/source/tree_print_failure.h"
#include <exception>
#include <algorithm>
namespace abcd::source {
// Synchronous diagnostic scope, isolated per thread and nested invocation.
// Never changes a charge, catches an error, or supplies recovery admission.
// Lower enum-only checkers can publish the *actual* failed charge without
// changing their error classification or allocating a diagnostic buffer.
struct TreePrintCensus {std::size_t methods=0,expressions=0,statements=0,operations=0,anchors=0;};
class TreePrintCensusScope {
 public:
  explicit TreePrintCensusScope(TreePrintCensus& census):previous_(active_) {active_=&census;}
  ~TreePrintCensusScope(){active_=previous_;}
  template<class Tree> static void Visit(const Tree& t) {if(active_){++active_->methods;active_->expressions+=t.expressions.size();active_->statements+=t.statements.size();active_->operations+=t.operations.size();active_->anchors+=t.anchors.size();}}
 private:
  inline static thread_local TreePrintCensus* active_=nullptr;
  TreePrintCensus* previous_;
};
class TreePrintTrace {
 public:
  explicit TreePrintTrace(bool inherit=false) : previous_(active_) {
    if(inherit && previous_) {failure=previous_->failure;failure.budget.reset();failure.cause={};}
    active_=this;
  }
  ~TreePrintTrace() { active_=previous_; }
  TreePrintTrace(const TreePrintTrace&)=delete;
  static TreePrintTrace* Active() { return active_; }
  void Propagate() const {if(previous_ && failure.budget)previous_->failure=failure;}
  TreePrintFailure Capture(TreePrintError kind) const { auto f=failure;f.kind=kind;return f; }
  TreePrintFailure failure{TreePrintError::kResourceLimit};
 private:
  inline static thread_local TreePrintTrace* active_=nullptr;
  TreePrintTrace* previous_;
};
inline void PrintCharge(TreePrintBudgetKind kind,std::optional<std::uint64_t> requested,
    std::uint64_t remaining,std::uint64_t limit,std::uint64_t peak=0) {
  if(auto* trace=TreePrintTrace::Active())
    trace->failure.budget=TreeBudgetEvidence{kind,requested,remaining,limit,limit-remaining,std::max(peak,limit-remaining),limit-remaining};
}
inline void PrintByteCharge(std::size_t count,std::size_t width,std::size_t remaining,
    std::size_t limit,std::size_t peak=0) {
  if(width && count>std::numeric_limits<std::size_t>::max()/width)
    PrintCharge(TreePrintBudgetKind::kCapacityOverflow,{},remaining,limit,peak);
  else PrintCharge(TreePrintBudgetKind::kScratchBytes,count*width,remaining,limit,peak);
}
class TreePrintLocationScope {
 public:
  template<class Tree> explicit TreePrintLocationScope(const Tree& tree):trace_(TreePrintTrace::Active()) {
    if(trace_){old_=trace_->failure.location;owner_=trace_->failure.owner;trace_->failure.location={};
      trace_->failure.location.method=tree.method;trace_->failure.owner=tree.owner;}
  }
  ~TreePrintLocationScope(){if(trace_&&!trace_->failure.budget&&std::uncaught_exceptions()==exceptions_){trace_->failure.location=old_;trace_->failure.owner=owner_;}}
 private:
  int exceptions_=std::uncaught_exceptions();
  TreePrintTrace* trace_;TreeLocation old_;std::optional<FunctionOwnerId> owner_;
};
class TreePrintSiteScope {
 public:
  explicit TreePrintSiteScope(TreePrintSite site):trace_(TreePrintTrace::Active()) {
    if(trace_){old_=trace_->failure.site;trace_->failure.site=site;}
  }
  ~TreePrintSiteScope(){if(trace_ && !trace_->failure.budget&&std::uncaught_exceptions()==exceptions_)trace_->failure.site=old_;}
 private:
  int exceptions_=std::uncaught_exceptions();
  TreePrintTrace* trace_;TreePrintSite old_=TreePrintSite::kPublication;
};
}
#endif
