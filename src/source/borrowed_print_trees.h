#ifndef ABCD_SOURCE_BORROWED_PRINT_TREES_H_
#define ABCD_SOURCE_BORROWED_PRINT_TREES_H_
#include "print_memory.h"
#include "function_family.h"
#include <functional>
#include <bit>
namespace abcd::source {
// Exact immutable identities covered by a still-held owner charge. The record
// issues this after retaining the constructed root/family. Nested class owners
// need their own still-held destination/input charge; no ancestry-based waiver.
class BorrowedPrintTrees {
  struct Entry {const FunctionTree* tree=nullptr;const PrintMemory::Lease* charge=nullptr;std::size_t required=0;};
 public:
  BorrowedPrintTrees(const FunctionTree* tree,const ConstructedFamily* family,
      const PrintMemory::Lease& owner,PrintMemory& scratch,analysis::SemanticBudget budget)
      :previous_(active_),trees_(scratch) {
    const auto count=family?family->members.size():1;
    trees_.Resize(count);
    for(std::size_t i=0;i<count;++i) {
      const auto lookup_work=1+std::bit_width(previous_?previous_->trees_.Size():std::size_t{0});
      if(lookup_work>budget.work-work_)throw TreePrintFailure{TreePrintError::kWorkLimit};
      work_+=lookup_work;
      const auto* input=family?&family->members[i].tree:tree;
      if(!input)throw TreePrintFailure{TreePrintError::kUnsupportedContext};
      if(const auto* inherited=Find(*input)){trees_[i]=*inherited;continue;}
      auto measured=MeasureTreeStorage(*input,{owner.Bytes()-bytes_,budget.work-work_});
      if(const auto* error=std::get_if<TreeStorageError>(&measured)) {
        const auto kind=*error==TreeStorageError::kWorkLimit?TreePrintError::kWorkLimit:TreePrintError::kResourceLimit;
        throw TreePrintTrace::Active()?TreePrintTrace::Active()->Capture(kind):TreePrintFailure{kind};
      }
      const auto& storage=std::get<TreeStorage>(measured);bytes_+=storage.bytes;work_+=storage.work;
      trees_[i]={input,&owner,0};
    }
    for(std::size_t i=0;i<count;++i)if(trees_[i].charge==&owner)trees_[i].required=bytes_;
    std::sort(trees_.Data(),trees_.Data()+count,[&](auto a,auto b){
      if(work_==budget.work)throw TreePrintFailure{TreePrintError::kWorkLimit};++work_;
      return std::less<const FunctionTree*>{}(a.tree,b.tree);
    });
    active_=this;
  }
  ~BorrowedPrintTrees(){active_=previous_;}
  BorrowedPrintTrees(const BorrowedPrintTrees&)=delete;
  static bool Holds(const FunctionTree& tree) {return Find(tree)!=nullptr;}
  static const Entry* Find(const FunctionTree& tree) {
    const auto* scope=active_;if(!scope)return nullptr;
    const auto* begin=scope->trees_.Data();const auto* end=begin+scope->trees_.Size();
    const auto* found=std::lower_bound(begin,end,&tree,[](const auto& entry,auto key){return std::less<const FunctionTree*>{}(entry.tree,key);});
    return found!=end&&found->tree==&tree&&found->charge->Bytes()>=found->required?found:nullptr;
  }
  std::uint64_t Work()const{return work_;}
 private:
  inline static thread_local const BorrowedPrintTrees* active_=nullptr;
  const BorrowedPrintTrees* previous_;
  PrintArray<Entry> trees_;
  std::size_t bytes_=0;std::uint64_t work_=0;
};
}
#endif
