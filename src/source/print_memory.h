#ifndef ABCD_SOURCE_PRINT_MEMORY_H_
#define ABCD_SOURCE_PRINT_MEMORY_H_
#include "tree_print_trace.h"
#include <cassert>
#include <memory>
#include <utility>
namespace abcd::source {
// A phase ledger. Leases reserve before allocation; attached allocations must
// be destroyed before credit can be returned. Peak never decreases.
class PrintMemory {
 public:
  explicit PrintMemory(std::size_t limit):limit_(limit){}
  class Lease {
   public:
    Lease()=default;
    Lease(Lease&& other) noexcept {Swap(other);}
    Lease& operator=(Lease&& other) noexcept {if(this!=&other){if(!Release())std::terminate();Swap(other);}return *this;}
    Lease(const Lease&)=delete;
    ~Lease(){const bool released=Release();assert(released);(void)released;}
    bool Release() {
      if(resident_)return false;
      if(owner_){owner_->live_-=bytes_;owner_=nullptr;bytes_=0;}
      return true;
    }
    void Allocated(std::size_t bytes){assert(owner_&&bytes<=bytes_-resident_);resident_+=bytes;owner_->tracked_+=bytes;owner_->Check();}
    void Freed(std::size_t bytes){assert(owner_&&bytes<=resident_);resident_-=bytes;owner_->tracked_-=bytes;owner_->Check();}
    std::size_t Bytes()const{return bytes_;}
    // Transfer, not release: no more admission; SourceText owns the allocation.
    void Publish(){assert(owner_);owner_->sealed_=true;owner_=nullptr;bytes_=resident_=0;}
   private:
    friend class PrintMemory;
    Lease(PrintMemory& owner,std::size_t bytes):owner_(&owner),bytes_(bytes){}
    void Swap(Lease& other)noexcept{std::swap(owner_,other.owner_);std::swap(bytes_,other.bytes_);std::swap(resident_,other.resident_);}
    PrintMemory* owner_=nullptr;
    std::size_t bytes_=0,resident_=0;
  };
  Lease Reserve(std::size_t bytes) {
    if(sealed_)throw TreePrintFailure{TreePrintError::kUnsupportedContext};
    if(bytes>Remaining()) {
      PrintByteCharge(bytes,1,Remaining(),limit_,peak_);
      throw TreePrintTrace::Active()?TreePrintTrace::Active()->Capture(TreePrintError::kResourceLimit):TreePrintFailure{TreePrintError::kResourceLimit};
    }
    live_+=bytes;peak_=std::max(peak_,live_);Check();return Lease(*this,bytes);
  }
  // A nested, bounded phase finished; its buffers have been destroyed. Retain
  // the observed peak without pretending those buffers remain live.
  void Observe(std::size_t peak) {auto lease=Reserve(peak);}
  std::size_t Remaining()const{return limit_-live_;}
  std::size_t Live()const{return live_;}
  std::size_t Peak()const{return peak_;}
  std::size_t Tracked()const{return tracked_;}
  bool Invariant()const{return tracked_<=live_&&live_<=limit_;}
 private:
  void Check()const{assert(Invariant());}
  std::size_t limit_,live_=0,peak_=0,tracked_=0;
  bool sealed_=false;
};
// Exact allocation, including simultaneous old/new buffers during growth.
// The lease is declared first, so storage is released before its credit.
template<class T> class PrintArray {
  static_assert(std::is_trivially_copyable_v<T>);
 public:
  explicit PrintArray(PrintMemory& memory):memory_(memory){}
  ~PrintArray(){Clear();}
  PrintArray(const PrintArray&)=delete;
  void Resize(std::size_t count) {
    if(count>SIZE_MAX/sizeof(T)) {
      PrintCharge(TreePrintBudgetKind::kCapacityOverflow,{},memory_.Remaining(),memory_.Remaining()+memory_.Live(),memory_.Peak());
      throw TreePrintTrace::Active()?TreePrintTrace::Active()->Capture(TreePrintError::kResourceLimit):TreePrintFailure{TreePrintError::kResourceLimit};
    }
    auto next=memory_.Reserve(count*sizeof(T));
    auto data=std::make_unique<T[]>(count);next.Allocated(count*sizeof(T));
    for(std::size_t i=0;i<std::min(size_,count);++i)data[i]=std::move(data_[i]);
    Clear();lease_=std::move(next);data_=std::move(data);size_=count;
  }
  T& operator[](std::size_t i){return data_[i];}
  const T& operator[](std::size_t i)const{return data_[i];}
  T* Data(){return data_.get();}
  const T* Data()const{return data_.get();}
  std::size_t Size()const{return size_;}
 private:
  void Clear(){data_.reset();if(size_)lease_.Freed(size_*sizeof(T));size_=0;const bool released=lease_.Release();assert(released);(void)released;}
  PrintMemory& memory_;PrintMemory::Lease lease_;std::unique_ptr<T[]> data_;std::size_t size_=0;
};
}
#endif
